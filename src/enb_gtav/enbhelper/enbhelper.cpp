//=============================================================================
//  enbhelper.cpp — GTA V ENBSeries Helper DLL (Rebuilt from RE analysis)
//
//  Uses ScriptHookV to read GTA V game state each frame and exposes it
//  via 11 exported functions called by the ENB d3d11.dll.
//
//  Native hashes verified against alloc8or/gta5-nativedb-data (2026-03-13).
//  All 12 hashes match the canonical database.
//
//  Build: Requires ScriptHookV.lib (from ScriptHookV SDK) to resolve imports.
//  Output: enbhelper.dll, placed in game_dir/enbseries/
//=============================================================================

#include <windows.h>
#include <cstdlib>
#include <cstdint>

// ScriptHookV API (6 imports, C++ mangled in the real DLL)
// For building without the full SDK, use our minimal declarations.
// If you have the real ScriptHookV SDK, replace with:
//   #include "ScriptHookV/inc/main.h"
//   #include "ScriptHookV/inc/natives.h"
#include "scripthookv_min.h"

// ═══════════════════════════════════════════════════════════════════════════
//  GTA V Native Hash Constants (verified against alloc8or nativedb)
// ═══════════════════════════════════════════════════════════════════════════

// CLOCK namespace
static constexpr uint64_t NATIVE_GET_CLOCK_HOURS   = 0x25223CA6B4D20B7F;
static constexpr uint64_t NATIVE_GET_CLOCK_MINUTES = 0x13D2B8ADD79640F2;
static constexpr uint64_t NATIVE_GET_CLOCK_SECONDS = 0x494E97C2EF27C470;

// MISC namespace
static constexpr uint64_t NATIVE_GET_PREV_WEATHER  = 0x564B884A05EC45A3;
static constexpr uint64_t NATIVE_GET_NEXT_WEATHER  = 0x711327CD09C8F162;
static constexpr uint64_t NATIVE_GET_CURR_WEATHER_STATE = 0xF3BBE884A14BB413;
static constexpr uint64_t NATIVE_GET_WIND_SPEED    = 0xA8CF1CC0AFCD3F12;
static constexpr uint64_t NATIVE_GET_WIND_DIR      = 0x1F400FEF721170DA;
static constexpr uint64_t NATIVE_GET_RAIN_LEVEL    = 0x96695E368AD855F3;
static constexpr uint64_t NATIVE_GET_SNOW_LEVEL    = 0xC5868A966E5BE3AE;

// INTERIOR namespace
static constexpr uint64_t NATIVE_IS_INTERIOR_SCENE = 0xBC72B5D7A1CBD54D;

// CAM namespace
static constexpr uint64_t NATIVE_GET_FINAL_CAM_COORD = 0xA200EB1EE790F448;

// ═══════════════════════════════════════════════════════════════════════════
//  Global State (written by Update, read by exports)
// ═══════════════════════════════════════════════════════════════════════════

static float g_time              = 12.0f;
static float g_weatherTransition = 0.0f;
static float g_snowAmount        = 0.0f;
static float g_cameraPosition[3] = {};
static int   g_currentWeather    = 0;
static float g_windSpeed         = 0.0f;
static float g_rainAmount        = 0.0f;
static int   g_outgoingWeather   = 0;
static int   g_isInterior        = 0;
static float g_windDirection[3]  = {};

// ScriptHookV returns Vector3 with 8-byte stride per component
struct NativeVector3 { float x; DWORD _0; float y; DWORD _1; float z; DWORD _2; };

// ═══════════════════════════════════════════════════════════════════════════
//  Update — called each script tick
// ═══════════════════════════════════════════════════════════════════════════

static void Update()
{
    uint64_t* r;

    // Time = hours + minutes/60 + seconds/3600
    nativeInit(NATIVE_GET_CLOCK_HOURS);   r = nativeCall(); float hours = static_cast<float>(*reinterpret_cast<int*>(r));
    nativeInit(NATIVE_GET_CLOCK_MINUTES); r = nativeCall(); int minutes = *reinterpret_cast<int*>(r);
    nativeInit(NATIVE_GET_CLOCK_SECONDS); r = nativeCall(); int seconds = *reinterpret_cast<int*>(r);

    float t = hours + static_cast<float>(minutes) / 60.0f + static_cast<float>(seconds) / 3600.0f;
    g_time = (t >= 0.0f && t < 24.0f) ? t : 0.0f;

    // Weather state
    nativeInit(NATIVE_GET_PREV_WEATHER); r = nativeCall(); int prev = *reinterpret_cast<int*>(r);
    nativeInit(NATIVE_GET_NEXT_WEATHER); r = nativeCall(); int next = *reinterpret_cast<int*>(r);

    float trans = 0.0f;
    nativeInit(NATIVE_GET_CURR_WEATHER_STATE);
    nativePush64(*reinterpret_cast<uint64_t*>(&prev));
    nativePush64(*reinterpret_cast<uint64_t*>(&next));
    nativePush64(*reinterpret_cast<uint64_t*>(&trans));
    nativeCall();

    g_currentWeather = next;
    g_outgoingWeather = prev;
    g_weatherTransition = (trans < 0.0f) ? 0.0f : (trans > 1.0f) ? 1.0f : trans;

    // Wind speed
    nativeInit(NATIVE_GET_WIND_SPEED); r = nativeCall();
    float ws = *reinterpret_cast<float*>(r);
    g_windSpeed = (ws < 0.0f) ? 0.0f : (ws > 10000.0f) ? 10000.0f : ws;

    // Wind direction (Vector3)
    nativeInit(NATIVE_GET_WIND_DIR); r = nativeCall();
    auto* wd = reinterpret_cast<NativeVector3*>(r);
    g_windDirection[0] = wd->x; g_windDirection[1] = wd->y; g_windDirection[2] = wd->z;

    // Rain level
    nativeInit(NATIVE_GET_RAIN_LEVEL); r = nativeCall();
    float rain = *reinterpret_cast<float*>(r);
    g_rainAmount = (rain < 0.0f) ? 0.0f : (rain > 1.0f) ? 1.0f : rain;

    // Snow level
    nativeInit(NATIVE_GET_SNOW_LEVEL); r = nativeCall();
    float snow = *reinterpret_cast<float*>(r);
    g_snowAmount = (snow < 0.0f) ? 0.0f : (snow > 1.0f) ? 1.0f : snow;

    // Interior
    nativeInit(NATIVE_IS_INTERIOR_SCENE); r = nativeCall();
    g_isInterior = *reinterpret_cast<int*>(r);

    // Camera position (Vector3)
    nativeInit(NATIVE_GET_FINAL_CAM_COORD); r = nativeCall();
    auto* cam = reinterpret_cast<NativeVector3*>(r);
    g_cameraPosition[0] = cam->x; g_cameraPosition[1] = cam->y; g_cameraPosition[2] = cam->z;
}

// ═══════════════════════════════════════════════════════════════════════════
//  Script main loop (runs as ScriptHookV fiber)
// ═══════════════════════════════════════════════════════════════════════════

static void ScriptMain()
{
    srand(GetTickCount());
    while (true) { Update(); scriptWait(0); }
}

// ═══════════════════════════════════════════════════════════════════════════
//  Exports — called by ENB d3d11.dll to read cached game state
// ═══════════════════════════════════════════════════════════════════════════

extern "C" {

__declspec(dllexport) BOOL GetTime(float* out)              { *out = g_time; return TRUE; }
__declspec(dllexport) BOOL GetWeatherTransition(float* out) { *out = g_weatherTransition; return TRUE; }
__declspec(dllexport) BOOL GetCurrentWeather(int* out)      { *out = g_currentWeather; return TRUE; }
__declspec(dllexport) BOOL GetOutgoingWeather(int* out)     { *out = g_outgoingWeather; return TRUE; }
__declspec(dllexport) BOOL GetWindSpeed(float* out)         { *out = g_windSpeed; return TRUE; }
__declspec(dllexport) BOOL GetRainAmount(float* out)        { *out = g_rainAmount; return TRUE; }
__declspec(dllexport) BOOL GetSnowAmount(float* out)        { *out = g_snowAmount; return TRUE; }
__declspec(dllexport) BOOL IsLoaded()                       { return TRUE; }

__declspec(dllexport) BOOL GetWindDirection(float* out)
{
    if (!out) return FALSE;
    out[0] = g_windDirection[0]; out[1] = g_windDirection[1]; out[2] = g_windDirection[2];
    return TRUE;
}

__declspec(dllexport) BOOL IsInterior(int* out)
{
    *out = g_isInterior;
    return TRUE;
}

__declspec(dllexport) BOOL GetCameraPosition(float* out)
{
    if (!out) return FALSE;
    out[0] = g_cameraPosition[0]; out[1] = g_cameraPosition[1]; out[2] = g_cameraPosition[2];
    return TRUE;
}

} // extern "C"

// ═══════════════════════════════════════════════════════════════════════════
//  DllMain
// ═══════════════════════════════════════════════════════════════════════════

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
        scriptRegister(hModule, ScriptMain);
    else if (reason == DLL_PROCESS_DETACH)
        scriptUnregister(hModule);
    return TRUE;
}
