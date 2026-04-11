#pragma once
//=============================================================================
//  ENBHelperBridge.h — Dynamic loader for enbhelper.dll
//
//  enbhelper.dll is a ScriptHookV plugin that reads GTA V game state
//  (time, weather, camera, wind, rain/snow, interior) and exposes it
//  via 11 simple exported functions.
//
//  This bridge dynamically loads the DLL and calls the exports each frame.
//=============================================================================

#include <Windows.h>

class ENBHelperBridge
{
public:
    bool Load(const char* gameDir);
    void Unload();

    // Call all helper functions to update game state
    // Should be called once per frame in API_BeforePresent
    void UpdateGameState();

    bool IsLoaded()     const { return m_loaded; }
    bool HasFailed()    const { return m_failed; }

    // Cached game state (updated by UpdateGameState)
    float  time              = 12.0f;
    float  weatherTransition = 0.0f;
    int    currentWeather    = 0;
    int    outgoingWeather   = 0;
    float  windSpeed         = 0.0f;
    float  windDirection[3]  = {};
    float  rainAmount        = 0.0f;
    float  snowAmount        = 0.0f;
    int    isInterior        = 0;
    float  cameraPosition[3] = {};

private:
    HMODULE m_dll = nullptr;
    bool    m_loaded = false;
    bool    m_failed = false;

    // Function pointers (resolved by Load)
    typedef BOOL (*PFN_GetFloat)(float*);
    typedef BOOL (*PFN_GetInt)(int*);
    typedef BOOL (*PFN_GetVec3)(float*);
    typedef BOOL (*PFN_IsLoaded)();

    PFN_GetFloat  m_getTime              = nullptr;
    PFN_GetFloat  m_getWeatherTransition = nullptr;
    PFN_GetInt    m_getCurrentWeather     = nullptr;
    PFN_GetInt    m_getOutgoingWeather    = nullptr;
    PFN_GetFloat  m_getWindSpeed         = nullptr;
    PFN_GetVec3   m_getWindDirection     = nullptr;
    PFN_GetFloat  m_getRainAmount        = nullptr;
    PFN_GetFloat  m_getSnowAmount        = nullptr;
    PFN_GetInt    m_isInterior           = nullptr;
    PFN_GetVec3   m_getCameraPosition    = nullptr;
    PFN_IsLoaded  m_isLoadedFn           = nullptr;
};

extern ENBHelperBridge g_Helper;
