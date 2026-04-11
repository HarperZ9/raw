#pragma once
//=============================================================================
//  WeatherSystem.h — Weather Parameter Blending
//
//  Maps GTA V weather hashes to weather IDs via _weatherlist.ini,
//  loads per-weather parameter overrides, and blends between current
//  and outgoing weather during transitions.
//=============================================================================

#include <Windows.h>
#include <unordered_map>
#include <string>

class WeatherSystem
{
public:
    void Initialize(const char* gameDir);

    // Update weather state from ENBHelper bridge data
    void Update(DWORD currentWeatherHash, DWORD outgoingWeatherHash,
                float transition);

    // Get the weather index (1-999) for a hash, or 0 if unknown
    int  GetWeatherIndex(DWORD weatherHash) const;

    // Register a weather hash -> index mapping at runtime
    void RegisterWeatherHash(DWORD hash, int index);

    // Get the per-weather INI path for a weather index (empty if none)
    const char* GetWeatherINIPath(int weatherIndex) const;

    // Current state
    int   GetCurrentIndex()    const { return m_currentIndex; }
    int   GetOutgoingIndex()   const { return m_outgoingIndex; }
    float GetTransition()      const { return m_transition; }
    bool  IsMultipleWeathers() const { return m_enableMultiple; }

private:
    void LoadWeatherList();

    char m_gameDir[MAX_PATH] = {};
    char m_weatherListPath[MAX_PATH] = {};

    bool m_enableMultiple = false;

    // Weather hash -> index (populated at runtime as hashes are seen)
    std::unordered_map<DWORD, int> m_hashToIndex;

    // Weather index -> INI filename
    std::unordered_map<int, std::string> m_indexToFile;

    // Current state
    int   m_currentIndex  = 0;
    int   m_outgoingIndex = 0;
    float m_transition    = 0.0f;

    // Full paths for per-weather INI files
    mutable char m_tempPath[MAX_PATH] = {};
};

extern WeatherSystem g_Weather;
