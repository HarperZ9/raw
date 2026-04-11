#pragma once
//=============================================================================
//  PluginLoader.h — ENB .dllplugin Scanner and Loader
//
//  Scans enbseries/*.dllplugin files, loads each via LoadLibraryA.
//  Plugins self-register by calling ENBSetCallbackFunction via
//  GetProcAddress on the d3d11.dll (us).
//=============================================================================

#include <Windows.h>
#include <vector>
#include <string>

struct LoadedPlugin
{
    std::string  filename;
    HMODULE      module = nullptr;
};

class PluginLoader
{
public:
    void LoadAll(const char* gameDir);
    void UnloadAll();

    int GetPluginCount() const { return static_cast<int>(m_plugins.size()); }
    const std::vector<LoadedPlugin>& GetPlugins() const { return m_plugins; }

private:
    std::vector<LoadedPlugin> m_plugins;
};

extern PluginLoader g_Plugins;
