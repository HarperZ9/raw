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
struct WeatherSurfaces { float wetness = 0, puddles = 0, snow = 0; };
struct WeatherTimes
{
    float soakSeconds = 60.0f;      // dry to soaked in full rain
    float drySeconds = 240.0f;      // soaked to dry under a clear sky (twice as long overcast)
    float puddleSeconds = 180.0f;   // puddles to full in full rain
    float drainSeconds = 600.0f;    // full puddles to none
    float snowSeconds = 300.0f;     // bare to deep in full snowfall
    float meltSeconds = 1800.0f;    // deep to bare with nothing falling (rain melts it in a tenth of that)
};
void WeatherDrive_SetTest(float rain, float snow);   // the ini stand-in; negative: off
void WeatherDrive_SetTimes(const WeatherTimes& t);
// Advances the surfaces by dt seconds. False while no weather has been reported (the ini values stay).
bool WeatherDrive_Step(double dt, WeatherSurfaces& out);
} // namespace SB::Proxy

// rain, snow: falling now, 0 to 1; cloud: cover 0 to 1; fog: 0 to 1. From the engine module, any thread.
extern "C" __declspec(dllexport) void PG_SetWeatherState(float rain, float snow, float cloud, float fog);
