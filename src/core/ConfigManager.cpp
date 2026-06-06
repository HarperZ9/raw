//=============================================================================
//  ConfigManager.cpp — Persistent settings for RAW
//  Copyright (c) 2026 Zain D. Harper (papacr0w). All rights reserved.
//=============================================================================

#include "ConfigManager.h"
#include "RangeOracle.h"   // Tier 1.3 gate (set from [Diagnostics] GpuReadback)
#include "GTAORenderer.h"
#include "ContactShadowRenderer.h"
#include "SkylightingRenderer.h"
#include "SSRRenderer.h"
#include "SSGIRenderer.h"
#include "SceneCompositor.h"
#include "ToneMapManager.h"
#include "BloomRenderer.h"
#include "DoFRenderer.h"
#include "ColorPipeline.h"
#include "LensRenderer.h"
#include "HiZPyramid.h"
#include "ClusteredLighting.h"
#include <SKSE/SKSE.h>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <algorithm>

namespace SB
{

// Simple INI parser — no external dependencies
struct IniData {
    // section -> { key -> value }
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> sections;
};

static IniData s_ini;

static void ParseIni(const std::filesystem::path& path)
{
    s_ini.sections.clear();
    std::ifstream file(path);
    if (!file.is_open()) return;

    std::string currentSection = "General";
    std::string line;

    while (std::getline(file, line)) {
        // Trim whitespace
        auto start = line.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) continue;
        line = line.substr(start);

        // Skip comments
        if (line[0] == ';' || line[0] == '#') continue;

        // Section header
        if (line[0] == '[') {
            auto end = line.find(']');
            if (end != std::string::npos)
                currentSection = line.substr(1, end - 1);
            continue;
        }

        // Key=Value
        auto eq = line.find('=');
        if (eq != std::string::npos) {
            std::string key = line.substr(0, eq);
            std::string val = line.substr(eq + 1);
            // Trim
            while (!key.empty() && (key.back() == ' ' || key.back() == '\t')) key.pop_back();
            auto vs = val.find_first_not_of(" \t");
            if (vs != std::string::npos) val = val.substr(vs);
            // Remove inline comments
            auto sc = val.find(';');
            if (sc != std::string::npos) val = val.substr(0, sc);
            while (!val.empty() && (val.back() == ' ' || val.back() == '\t')) val.pop_back();

            s_ini.sections[currentSection][key] = val;
        }
    }
}

static void WriteIni(const std::filesystem::path& path)
{
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);

    std::ofstream file(path);
    if (!file.is_open()) return;

    file << "; RAW v1.0 Configuration\n";
    file << "; Copyright (c) 2026 Zain D. Harper (papacr0w)\n";
    file << "; Auto-saved. Edit while game is closed.\n\n";

    for (auto& [section, keys] : s_ini.sections) {
        file << "[" << section << "]\n";
        for (auto& [key, val] : keys) {
            file << key << " = " << val << "\n";
        }
        file << "\n";
    }
}

float ConfigManager::ReadFloat(const std::string& section, const std::string& key, float def)
{
    auto sit = s_ini.sections.find(section);
    if (sit == s_ini.sections.end()) return def;
    auto kit = sit->second.find(key);
    if (kit == sit->second.end()) return def;
    try { return std::stof(kit->second); } catch (...) { return def; }
}

int ConfigManager::ReadInt(const std::string& section, const std::string& key, int def)
{
    auto sit = s_ini.sections.find(section);
    if (sit == s_ini.sections.end()) return def;
    auto kit = sit->second.find(key);
    if (kit == sit->second.end()) return def;
    try { return std::stoi(kit->second); } catch (...) { return def; }
}

bool ConfigManager::ReadBool(const std::string& section, const std::string& key, bool def)
{
    auto sit = s_ini.sections.find(section);
    if (sit == s_ini.sections.end()) return def;
    auto kit = sit->second.find(key);
    if (kit == sit->second.end()) return def;
    auto& v = kit->second;
    return (v == "1" || v == "true" || v == "yes" || v == "True");
}

void ConfigManager::WriteFloat(const std::string& section, const std::string& key, float val)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "%.4f", val);
    s_ini.sections[section][key] = buf;
}

void ConfigManager::WriteInt(const std::string& section, const std::string& key, int val)
{
    s_ini.sections[section][key] = std::to_string(val);
}

void ConfigManager::WriteBool(const std::string& section, const std::string& key, bool val)
{
    s_ini.sections[section][key] = val ? "true" : "false";
}

void ConfigManager::Initialize(const std::filesystem::path& configPath)
{
    m_configPath = configPath;
    m_initialized = true;
    Load();
    SKSE::log::info("ConfigManager: initialized — {}", configPath.string());
}

void ConfigManager::Load()
{
    if (m_configPath.empty()) return;

    std::error_code ec;
    if (!std::filesystem::exists(m_configPath, ec)) {
        SKSE::log::info("ConfigManager: no config file found — using defaults");
        return;
    }

    ParseIni(m_configPath);
    SKSE::log::info("ConfigManager: loaded config from {}", m_configPath.string());
}

void ConfigManager::ApplyToSystems()
{
    if (!m_initialized) return;

    RangeOracle::SetEnabled(ReadBool("Diagnostics", "GpuReadback", false));  // Tier 1.3 per-pass range oracle, default OFF

    // ── GTAO ──
    auto& gtao = GTAORenderer::Get();
    if (gtao.IsInitialized()) {
        gtao.SetEnabled(ReadBool("GTAO", "Enabled", false));
        gtao.SetRadius(ReadFloat("GTAO", "Radius", 1.5f));
        gtao.SetIntensity(ReadFloat("GTAO", "Intensity", 1.0f));
        gtao.SetDirections(ReadInt("GTAO", "Directions", 4));
        gtao.SetSteps(ReadInt("GTAO", "Steps", 8));
    }

    // ── Contact Shadows ──
    auto& cs = ContactShadowRenderer::Get();
    if (cs.IsInitialized()) {
        cs.SetEnabled(ReadBool("ContactShadows", "Enabled", false));
        cs.SetRayLength(ReadFloat("ContactShadows", "RayLength", 0.10f));
        cs.SetIntensity(ReadFloat("ContactShadows", "Intensity", 1.0f));
    }

    // ── Skylighting ──
    auto& sky = SkylightingRenderer::Get();
    if (sky.IsInitialized()) {
        sky.SetEnabled(ReadBool("Skylighting", "Enabled", false));
        sky.SetIntensity(ReadFloat("Skylighting", "Intensity", 1.0f));
    }

    // ── SSR ──
    auto& ssr = SSRRenderer::Get();
    if (ssr.IsInitialized()) {
        ssr.SetEnabled(ReadBool("SSR", "Enabled", false));
        ssr.SetIntensity(ReadFloat("SSR", "Intensity", 1.0f));
    }

    // ── SSGI ──
    auto& ssgi = SSGIRenderer::Get();
    if (ssgi.IsInitialized()) {
        ssgi.SetEnabled(ReadBool("SSGI", "Enabled", false));
        ssgi.SetGIIntensity(ReadFloat("SSGI", "Intensity", 0.25f));
    }

    // ── Scene Compositor ──
    auto& comp = SceneCompositor::Get();
    if (comp.IsInitialized()) {
        comp.SetEnabled(ReadBool("Compositor", "Enabled", true));
        comp.SetAOIntensity(ReadFloat("Compositor", "AOIntensity", 0.60f));
        comp.SetShadowIntensity(ReadFloat("Compositor", "ShadowIntensity", 0.80f));
        comp.SetSkylightIntensity(ReadFloat("Compositor", "SkylightIntensity", 0.50f));
        comp.SetGIIntensity(ReadFloat("Compositor", "GIIntensity", 0.15f));
        comp.SetSSRIntensity(ReadFloat("Compositor", "SSRIntensity", 0.30f));
    }

    // ── Bloom ──
    auto& bloom = BloomRenderer::Get();
    if (bloom.IsInitialized()) {
        bloom.SetEnabled(ReadBool("Bloom", "Enabled", false));
    }

    // ── Color Pipeline ──
    auto& cp = ColorPipeline::Get();
    if (cp.IsInitialized()) {
        cp.SetEnabled(ReadBool("ColorPipeline", "Enabled", false));
        cp.SetStageMask(static_cast<uint32_t>(ReadInt("ColorPipeline", "StageMask",
            static_cast<int>(CPS_Exposure | CPS_ToneMap | CPS_Dither))));
        cp.SetToneCurve(static_cast<ColorToneCurve>(ReadInt("ColorPipeline", "ToneCurve", 0)));
        cp.SetExposureCompensation(ReadFloat("ColorPipeline", "ExposureComp", 0.0f));
        cp.SetSaturation(ReadFloat("ColorPipeline", "Saturation", 1.0f));
        cp.SetSCurveContrast(ReadFloat("ColorPipeline", "Contrast", 1.0f));
        cp.SetWhiteBalanceTemp(ReadFloat("ColorPipeline", "WhiteBalance", 6500.0f));
        cp.SetOutputMode(ReadInt("ColorPipeline", "OutputMode", 0));
    }

    SKSE::log::info("ConfigManager: applied config to all systems");
}

void ConfigManager::Save()
{
    if (m_configPath.empty()) return;

    // ── Read current state from all systems ──

    auto& gtao = GTAORenderer::Get();
    if (gtao.IsInitialized()) {
        WriteBool("GTAO", "Enabled", gtao.IsEnabled());
        WriteFloat("GTAO", "Radius", gtao.GetRadius());
        WriteFloat("GTAO", "Intensity", gtao.GetIntensity());
        WriteInt("GTAO", "Directions", gtao.GetDirections());
        WriteInt("GTAO", "Steps", gtao.GetSteps());
    }

    auto& cs = ContactShadowRenderer::Get();
    if (cs.IsInitialized()) {
        WriteBool("ContactShadows", "Enabled", cs.IsEnabled());
        WriteFloat("ContactShadows", "RayLength", cs.GetRayLength());
        WriteFloat("ContactShadows", "Intensity", cs.GetIntensity());
    }

    auto& sky = SkylightingRenderer::Get();
    if (sky.IsInitialized()) {
        WriteBool("Skylighting", "Enabled", sky.IsEnabled());
        WriteFloat("Skylighting", "Intensity", sky.GetIntensity());
    }

    auto& ssr = SSRRenderer::Get();
    if (ssr.IsInitialized()) {
        WriteBool("SSR", "Enabled", ssr.IsEnabled());
        WriteFloat("SSR", "Intensity", ssr.GetIntensity());
    }

    auto& ssgi = SSGIRenderer::Get();
    if (ssgi.IsInitialized()) {
        WriteBool("SSGI", "Enabled", ssgi.IsEnabled());
        WriteFloat("SSGI", "Intensity", ssgi.GetGIIntensity());
    }

    auto& comp = SceneCompositor::Get();
    if (comp.IsInitialized()) {
        WriteBool("Compositor", "Enabled", comp.IsEnabled());
        WriteFloat("Compositor", "AOIntensity", comp.GetAOIntensity());
        WriteFloat("Compositor", "ShadowIntensity", comp.GetShadowIntensity());
        WriteFloat("Compositor", "SkylightIntensity", comp.GetSkylightIntensity());
        WriteFloat("Compositor", "GIIntensity", comp.GetGIIntensity());
        WriteFloat("Compositor", "SSRIntensity", comp.GetSSRIntensity());
    }

    auto& bloom = BloomRenderer::Get();
    if (bloom.IsInitialized()) {
        WriteBool("Bloom", "Enabled", bloom.IsEnabled());
    }

    auto& cp = ColorPipeline::Get();
    if (cp.IsInitialized()) {
        WriteBool("ColorPipeline", "Enabled", cp.IsEnabled());
        WriteInt("ColorPipeline", "StageMask", static_cast<int>(cp.GetStageMask()));
        WriteInt("ColorPipeline", "ToneCurve", static_cast<int>(cp.GetToneCurve()));
        WriteFloat("ColorPipeline", "ExposureComp", cp.GetExposureCompensation());
        WriteFloat("ColorPipeline", "Saturation", cp.GetSaturation());
        WriteFloat("ColorPipeline", "Contrast", cp.GetSCurveContrast());
        WriteFloat("ColorPipeline", "WhiteBalance", cp.GetWhiteBalanceTemp());
        WriteInt("ColorPipeline", "OutputMode", cp.GetOutputMode());
    }

    WriteIni(m_configPath);
    SKSE::log::info("ConfigManager: saved config to {}", m_configPath.string());
}

void ConfigManager::SavePreset(int slot)
{
    if (slot < 1 || slot > 5 || m_configPath.empty()) return;
    auto presetPath = m_configPath.parent_path() / ("RAW_Preset" + std::to_string(slot) + ".ini");

    // Save current state to the main INI data, then write to preset path
    Save();  // updates s_ini
    WriteIni(presetPath);
    SKSE::log::info("ConfigManager: saved preset {} to {}", slot, presetPath.string());
}

void ConfigManager::LoadPreset(int slot)
{
    if (slot < 1 || slot > 5 || m_configPath.empty()) return;
    auto presetPath = m_configPath.parent_path() / ("RAW_Preset" + std::to_string(slot) + ".ini");

    std::error_code ec;
    if (!std::filesystem::exists(presetPath, ec)) {
        SKSE::log::warn("ConfigManager: preset {} not found", slot);
        return;
    }

    ParseIni(presetPath);
    ApplyToSystems();
    // Also update main config
    WriteIni(m_configPath);
    SKSE::log::info("ConfigManager: loaded preset {} from {}", slot, presetPath.string());
}

bool ConfigManager::PresetExists(int slot) const
{
    if (slot < 1 || slot > 5 || m_configPath.empty()) return false;
    auto presetPath = m_configPath.parent_path() / ("RAW_Preset" + std::to_string(slot) + ".ini");
    std::error_code ec;
    return std::filesystem::exists(presetPath, ec);
}

} // namespace SB
