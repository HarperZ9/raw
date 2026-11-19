// WeatherDrive (WeatherDrive.h). Original work, MIT.
#include "WeatherDrive.h"

#include <algorithm>
#include <atomic>

namespace SB::Proxy
{
namespace
{
struct Report { std::atomic<float> rain{0}, snow{0}, cloud{0}, fog{0}; std::atomic<bool> seen{false}; };
Report g_report;
float g_testRain = -1.0f, g_testSnow = -1.0f;
WeatherTimes g_times;
WeatherSurfaces g_s;

float Clamp01(float v) { return std::min(std::max(v, 0.0f), 1.0f); }
} // namespace

void WeatherDrive_SetTest(float rain, float snow) { g_testRain = rain; g_testSnow = snow; }
void WeatherDrive_SetTimes(const WeatherTimes& t) { g_times = t; }

bool WeatherDrive_Step(double dt, WeatherSurfaces& out)
{
    const bool test = g_testRain >= 0.0f || g_testSnow >= 0.0f;
    if (!test && !g_report.seen.load()) return false;
    const float rain = test ? std::max(g_testRain, 0.0f) : Clamp01(g_report.rain.load());
    const float snow = test ? std::max(g_testSnow, 0.0f) : Clamp01(g_report.snow.load());
    const float cloud = test ? (rain > 0.0f || snow > 0.0f ? 1.0f : 0.0f) : Clamp01(g_report.cloud.load());
    const float t = float(std::min(dt, 0.25));   // a stall or a loading screen does not jump the weather
    const WeatherTimes& k = g_times;
    if (rain > 0.05f) g_s.wetness += t * rain / k.soakSeconds;
    else g_s.wetness -= t / (k.drySeconds * (1.0f + cloud));
    if (rain > 0.3f) g_s.puddles += t * (rain - 0.3f) / (0.7f * k.puddleSeconds);
    else g_s.puddles -= t / k.drainSeconds;
    g_s.puddles = std::min(g_s.puddles, g_s.wetness);   // no standing water on dry ground
    if (snow > 0.05f) g_s.snow += t * snow / k.snowSeconds;
    else g_s.snow -= t / (rain > 0.05f ? 0.1f * k.meltSeconds : k.meltSeconds);
    g_s.wetness = Clamp01(g_s.wetness); g_s.puddles = Clamp01(g_s.puddles); g_s.snow = Clamp01(g_s.snow);
    g_s.rain = Clamp01(rain);
    g_s.cloud = cloud;
    out = g_s;
    return true;
}
} // namespace SB::Proxy

extern "C" __declspec(dllexport) void PG_SetWeatherState(float rain, float snow, float cloud, float fog)
{
    using SB::Proxy::g_report;
    g_report.rain = rain; g_report.snow = snow; g_report.cloud = cloud; g_report.fog = fog;
    g_report.seen = true;
}
