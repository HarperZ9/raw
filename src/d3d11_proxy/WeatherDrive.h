#pragma once
// WeatherDrive: the game's weather drives rain on surfaces, puddles and snow cover (Weather.hlsli).
// The engine module reports the weather each frame (PG_SetWeatherState: rain and snow falling now,
// cloud cover, fog); the surfaces follow with time: they soak while it rains and dry after, water
// gathers in puddles under steady rain, snow builds while it falls and melts in rain or slowly after.
// [Lighting] WeatherRain / WeatherSnow (0 to 1, -1 off) stand in for the game's report, for tests.
// Original work, MIT.
#include <cstdint>

namespace SB::Proxy
{
struct WeatherSurfaces {
    float wetness = 0, puddles = 0, snow = 0, rain = 0, cloud = 0;
    float windX = 0, windY = 0, lightning = 0, gameHour = 12.0f;
    bool hasExt = false;
};
struct WeatherTimes
{
    float soakSeconds = 15.0f;      // dry to soaked in full rain
    float drySeconds = 120.0f;      // soaked to dry under a clear sky (twice as long overcast)
    float puddleSeconds = 28.0f;    // puddles to full in full rain
    float drainSeconds = 240.0f;    // full puddles to none
    float snowSeconds = 22.0f;      // bare to deep snowdrifts in full snowfall
    float meltSeconds = 360.0f;     // deep to bare with nothing falling (rain melts it in a tenth of that)
};
void WeatherDrive_SetTest(float rain, float snow);   // the ini stand-in; negative: off
void WeatherDrive_SetTimes(const WeatherTimes& t);
void WeatherDrive_OnTeleport();
// Advances the surfaces by dt seconds. False while no weather has been reported (the ini values stay).
bool WeatherDrive_Step(double dt, WeatherSurfaces& out);
struct EngineCameraState {
    float camPos[3] = {};
    float camRot[9] = {};
    float fovDeg = 80.0f;
    float waterHeight = -14000.0f;
    uint32_t interior = 0;
    uint32_t cellId = 0;
    uint32_t worldId = 0;
    uint32_t camStateId = 0xFFFFFFFFu;
    uint32_t menuPaused = 0;
    bool valid = false;
};
bool EngineCamera_Get(EngineCameraState& out);
} // namespace SB::Proxy

// rain, snow: falling now, 0 to 1; cloud: cover 0 to 1; fog: 0 to 1. From the engine module, any thread.
extern "C" __declspec(dllexport) void PG_SetWeatherState(float rain, float snow, float cloud, float fog);
extern "C" __declspec(dllexport) void PG_SetWeatherExt(float windX, float windY, float lightning, float gameHour);
// Ground-truth NiCamera + TESObjectCELL / TESWorldSpace / UI state pushed directly from raw_engine.dll (rt_view.cpp).
extern "C" __declspec(dllexport) void PG_SetEngineCamera(const float camPos[3], const float camRot[9], float fovDeg,
                                                         uint32_t interior, uint32_t cellId, uint32_t worldId);
extern "C" __declspec(dllexport) void PG_SetEngineWorld(float waterHeight, uint32_t camStateId, uint32_t menuPaused);
