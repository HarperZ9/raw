//=============================================================================
//  PluginLoader.cpp — .dllplugin Scanner and Loader
//
//  Original ENB scans: %gameDir%\enbseries\*.dllplugin
//  Each plugin is a renamed DLL that links to ENBSeries SDK functions
//  via GetProcAddress at DLL_PROCESS_ATTACH time.
//=============================================================================

#include "PluginLoader.h"
#include <cstdio>

PluginLoader g_Plugins;

void PluginLoader::LoadAll(const char* gameDir)
{
    char searchPath[MAX_PATH];
    snprintf(searchPath, MAX_PATH, "%s\\enbseries\\*.dllplugin", gameDir);

    WIN32_FIND_DATAA findData;
    HANDLE hFind = FindFirstFileA(searchPath, &findData);
    if (hFind == INVALID_HANDLE_VALUE)
        return;

    do
    {
        if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            continue;

        char fullPath[MAX_PATH];
        snprintf(fullPath, MAX_PATH, "%s\\enbseries\\%s", gameDir, findData.cFileName);

        LoadedPlugin plugin;
        plugin.filename = findData.cFileName;

        // LoadLibraryA triggers the plugin's DllMain(DLL_PROCESS_ATTACH),
        // which typically calls EnumProcessModules to find our d3d11.dll,
        // then GetProcAddress("ENBGetSDKVersion") to verify, then
        // ENBSetCallbackFunction to register its callback.
        plugin.module = LoadLibraryA(fullPath);

        if (plugin.module)
        {
            char buf[512];
            snprintf(buf, sizeof(buf), "[ENB] Plugin loaded: %s\n", findData.cFileName);
            OutputDebugStringA(buf);
            m_plugins.push_back(plugin);
        }
        else
        {
            char buf[512];
            snprintf(buf, sizeof(buf), "[ENB] Plugin FAILED to load: %s (error %lu)\n",
                     findData.cFileName, GetLastError());
            OutputDebugStringA(buf);
        }

    } while (FindNextFileA(hFind, &findData));

    FindClose(hFind);

    if (!m_plugins.empty())
    {
        char buf[256];
        snprintf(buf, sizeof(buf), "[ENB] %d plugin(s) loaded\n",
                 static_cast<int>(m_plugins.size()));
        OutputDebugStringA(buf);
    }
}

void PluginLoader::UnloadAll()
{
    // Unload in reverse order
    for (auto it = m_plugins.rbegin(); it != m_plugins.rend(); ++it)
    {
        if (it->module)
        {
            FreeLibrary(it->module);
            it->module = nullptr;
        }
    }
    m_plugins.clear();
}
