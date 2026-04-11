//=============================================================================
//  ENBHelperBridge.cpp — Dynamic enbhelper.dll loader
//=============================================================================

#include "ENBHelperBridge.h"
#include <cstdio>

ENBHelperBridge g_Helper;

static void HelperLog(const char* fmt, ...)
{
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    OutputDebugStringA(buf);
}

bool ENBHelperBridge::Load(const char* gameDir)
{
    if (m_loaded) return true;
    if (m_failed) return false;

    // Try enbseries subfolder first, then game root
    char path[MAX_PATH];
    snprintf(path, MAX_PATH, "%s\\enbseries\\enbhelper.dll", gameDir);

    m_dll = LoadLibraryA(path);
    if (!m_dll)
    {
        snprintf(path, MAX_PATH, "%s\\enbhelper.dll", gameDir);
        m_dll = LoadLibraryA(path);
    }

    if (!m_dll)
    {
        HelperLog("[ENB] enbhelper.dll not found (ScriptHookV integration disabled)\n");
        m_failed = true;
        return false;
    }

    // Resolve all 11 exports
    m_getTime              = (PFN_GetFloat) GetProcAddress(m_dll, "GetTime");
    m_getWeatherTransition = (PFN_GetFloat) GetProcAddress(m_dll, "GetWeatherTransition");
    m_getCurrentWeather    = (PFN_GetInt)   GetProcAddress(m_dll, "GetCurrentWeather");
    m_getOutgoingWeather   = (PFN_GetInt)   GetProcAddress(m_dll, "GetOutgoingWeather");
    m_getWindSpeed         = (PFN_GetFloat) GetProcAddress(m_dll, "GetWindSpeed");
    m_getWindDirection     = (PFN_GetVec3)  GetProcAddress(m_dll, "GetWindDirection");
    m_getRainAmount        = (PFN_GetFloat) GetProcAddress(m_dll, "GetRainAmount");
    m_getSnowAmount        = (PFN_GetFloat) GetProcAddress(m_dll, "GetSnowAmount");
    m_isInterior           = (PFN_GetInt)   GetProcAddress(m_dll, "IsInterior");
    m_getCameraPosition    = (PFN_GetVec3)  GetProcAddress(m_dll, "GetCameraPosition");
    m_isLoadedFn           = (PFN_IsLoaded) GetProcAddress(m_dll, "IsLoaded");

    // Check that at least the critical functions resolved
    if (!m_getTime || !m_getCurrentWeather || !m_getWeatherTransition)
    {
        HelperLog("[ENB] ENBHELPER functions not found\n");
        FreeLibrary(m_dll);
        m_dll = nullptr;
        m_failed = true;
        return false;
    }

    m_loaded = true;
    HelperLog("[ENB] enbhelper.dll loaded from %s\n", path);
    return true;
}

void ENBHelperBridge::Unload()
{
    if (m_dll)
    {
        FreeLibrary(m_dll);
        m_dll = nullptr;
    }
    m_loaded = false;
}

void ENBHelperBridge::UpdateGameState()
{
    if (!m_loaded) return;

    // Check if the helper script has started (IsLoaded returns TRUE once
    // the ScriptHookV script thread has run at least one frame)
    if (m_isLoadedFn && !m_isLoadedFn())
        return;

    // Read all game state. Each function writes to the output pointer
    // and returns TRUE on success. We use __try to guard against
    // potential crashes if ScriptHookV is in a bad state.
    __try
    {
        if (m_getTime)              m_getTime(&time);
        if (m_getWeatherTransition) m_getWeatherTransition(&weatherTransition);
        if (m_getCurrentWeather)    m_getCurrentWeather(&currentWeather);
        if (m_getOutgoingWeather)   m_getOutgoingWeather(&outgoingWeather);
        if (m_getWindSpeed)         m_getWindSpeed(&windSpeed);
        if (m_getWindDirection)     m_getWindDirection(windDirection);
        if (m_getRainAmount)        m_getRainAmount(&rainAmount);
        if (m_getSnowAmount)        m_getSnowAmount(&snowAmount);
        if (m_isInterior)           m_isInterior(&isInterior);
        if (m_getCameraPosition)    m_getCameraPosition(cameraPosition);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        HelperLog("[ENB] ENBHELPER functions failed to work\n");
        m_loaded = false;
        m_failed = true;
    }
}
