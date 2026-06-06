#pragma once
//=============================================================================
//  ConfigManager — Persistent settings for RAW
//
//  Saves/loads all effect enable states and parameters to RAW.ini.
//  Auto-saves on change, auto-loads on startup.
//
//  INI path: Data/SKSE/Plugins/RAW/RAW.ini
//
//  Copyright (c) 2026 Zain D. Harper (papacr0w). All rights reserved.
//=============================================================================

#include <string>
#include <filesystem>

namespace SB
{

class ConfigManager
{
public:
    static ConfigManager& Get()
    {
        static ConfigManager inst;
        return inst;
    }

    /// Initialize with INI path. Loads existing config if present.
    void Initialize(const std::filesystem::path& configPath);

    /// Save all current effect states to INI.
    void Save();

    /// Load effect states from INI and apply to all systems.
    void Load();

    /// Apply loaded config to all rendering systems.
    /// Call after all renderers are initialized.
    void ApplyToSystems();

    /// Save current config to a named preset slot (1-5).
    void SavePreset(int slot);

    /// Load a named preset slot and apply.
    void LoadPreset(int slot);

    /// Check if a preset slot has a saved file.
    bool PresetExists(int slot) const;

    bool IsInitialized() const { return m_initialized; }

private:
    ConfigManager() = default;

    // INI read/write helpers
    float ReadFloat(const std::string& section, const std::string& key, float defaultVal);
    int   ReadInt(const std::string& section, const std::string& key, int defaultVal);
    bool  ReadBool(const std::string& section, const std::string& key, bool defaultVal);
    void  WriteFloat(const std::string& section, const std::string& key, float val);
    void  WriteInt(const std::string& section, const std::string& key, int val);
    void  WriteBool(const std::string& section, const std::string& key, bool val);

    std::filesystem::path m_configPath;
    bool m_initialized = false;
};

} // namespace SB
