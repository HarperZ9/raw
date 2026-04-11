//=============================================================================
//  WeatherSystem.cpp — Weather Parameter Blending Implementation
//=============================================================================

#include "WeatherSystem.h"
#include <cstdio>
#include <cstring>

WeatherSystem g_Weather;

void WeatherSystem::Initialize(const char* gameDir)
{
    strncpy_s(m_gameDir, gameDir, MAX_PATH - 1);
    snprintf(m_weatherListPath, MAX_PATH, "%s\\enbseries\\_weatherlist.ini", gameDir);
    LoadWeatherList();
}

void WeatherSystem::LoadWeatherList()
{
    // _weatherlist.ini has sections [WEATHER001] through [WEATHER999]
    // Each has a FileName= key pointing to the per-weather INI
    for (int i = 1; i <= 999; i++)
    {
        char section[32];
        snprintf(section, sizeof(section), "WEATHER%03d", i);

        char filename[MAX_PATH];
        GetPrivateProfileStringA(section, "FileName", "", filename, sizeof(filename),
                                  m_weatherListPath);

        if (filename[0] != '\0')
        {
            m_indexToFile[i] = filename;
        }
    }
}

void WeatherSystem::Update(DWORD currentWeatherHash, DWORD outgoingWeatherHash,
                            float transition)
{
    m_currentIndex  = GetWeatherIndex(currentWeatherHash);
    m_outgoingIndex = GetWeatherIndex(outgoingWeatherHash);
    m_transition    = transition;
}

int WeatherSystem::GetWeatherIndex(DWORD weatherHash) const
{
    if (weatherHash == 0)
        return 0;

    auto it = m_hashToIndex.find(weatherHash);
    if (it != m_hashToIndex.end())
        return it->second;

    return 0; // Unknown hash
}

void WeatherSystem::RegisterWeatherHash(DWORD hash, int index)
{
    if (hash != 0 && index > 0)
        m_hashToIndex[hash] = index;
}

const char* WeatherSystem::GetWeatherINIPath(int weatherIndex) const
{
    auto it = m_indexToFile.find(weatherIndex);
    if (it == m_indexToFile.end() || it->second.empty())
        return "";

    snprintf(m_tempPath, MAX_PATH, "%s\\enbseries\\%s", m_gameDir, it->second.c_str());
    return m_tempPath;
}
