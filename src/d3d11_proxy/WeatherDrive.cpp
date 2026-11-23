// WeatherDrive (WeatherDrive.h). Original work, MIT.
#include "WeatherDrive.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <mutex>

namespace SB::Proxy
{
namespace
{
struct Report {
    std::atomic<float> rain{0}, snow{0}, cloud{0}, fog{0};
    std::atomic<float> windX{0}, windY{0}, lightning{0}, gameHour{12.0f};
    std::atomic<bool> seen{false}, extSeen{false};
};
Report g_report;
std::mutex g_camMutex;
EngineCameraState g_engineCam;
uint32_t g_lastWorldId = 0, g_lastCellId = 0;
float g_testRain = -1.0f, g_testSnow = -1.0f;
WeatherTimes g_times;
WeatherSurfaces g_s;
bool g_snapNext = false;

float Clamp01(float v) { return std::min(std::max(v, 0.0f), 1.0f); }
} // namespace

void WeatherDrive_SetTest(float rain, float snow) { g_testRain = rain; g_testSnow = snow; }
void WeatherDrive_SetTimes(const WeatherTimes& t) { g_times = t; }
void WeatherDrive_OnTeleport() { g_snapNext = true; }

bool WeatherDrive_Step(double dt, WeatherSurfaces& out)
{
    const bool test = g_testRain >= 0.0f || g_testSnow >= 0.0f;
    if (!test && !g_report.seen.load()) return false;
    const float rain = test ? std::max(g_testRain, 0.0f) : Clamp01(g_report.rain.load());
    const float snow = test ? std::max(g_testSnow, 0.0f) : Clamp01(g_report.snow.load());
    const float cloud = test ? (rain > 0.0f || snow > 0.0f ? 1.0f : 0.0f) : Clamp01(g_report.cloud.load());
    // After a loading screen / fast-travel / cell teleport, immediately snap surface wetness/snow to the new
    // region's live TESWeather instead of carrying Dawnstar snow into sunny Whiterun.
    if (dt > 0.5 || g_snapNext) {
        g_snapNext = false;
        g_s.wetness = rain;
        g_s.puddles = rain > 0.3f ? (rain - 0.3f) / 0.7f * 0.5f : 0.0f;
        g_s.snow = snow;
    }
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
    if (g_report.extSeen.load()) {
        g_s.windX = g_report.windX.load();
        g_s.windY = g_report.windY.load();
        g_s.lightning = Clamp01(g_report.lightning.load());
        g_s.gameHour = g_report.gameHour.load();
        g_s.hasExt = true;
    }
    out = g_s;
    return true;
}

bool EngineCamera_Get(EngineCameraState& out)
{
    std::lock_guard<std::mutex> lock(g_camMutex);
    if (!g_engineCam.valid) return false;
    out = g_engineCam;
    return true;
}
} // namespace SB::Proxy

extern "C" __declspec(dllexport) void PG_SetEngineCamera(const float camPos[3], const float camRot[9], float fovDeg,
                                                         uint32_t interior, uint32_t cellId, uint32_t worldId)
{
    using namespace SB::Proxy;
    std::lock_guard<std::mutex> lock(g_camMutex);
    if (g_engineCam.valid && worldId != 0 && g_lastWorldId != 0 && worldId != g_lastWorldId) {
        WeatherDrive_OnTeleport();
    } else if (g_engineCam.valid && g_engineCam.interior != interior && interior == 0 && cellId != g_lastCellId) {
        WeatherDrive_OnTeleport();
    }
    if (camPos) std::memcpy(g_engineCam.camPos, camPos, 12);
    if (camRot) std::memcpy(g_engineCam.camRot, camRot, 36);
    g_engineCam.fovDeg = fovDeg;
    g_engineCam.interior = interior;
    g_engineCam.cellId = cellId;
    g_engineCam.worldId = worldId;
    g_engineCam.valid = true;
    if (worldId) g_lastWorldId = worldId;
    if (cellId) g_lastCellId = cellId;
}

extern "C" __declspec(dllexport) void PG_SetEngineWorld(float waterHeight, uint32_t camStateId, uint32_t menuPaused)
{
    using namespace SB::Proxy;
    std::lock_guard<std::mutex> lock(g_camMutex);
    g_engineCam.waterHeight = waterHeight;
    g_engineCam.camStateId = camStateId;
    g_engineCam.menuPaused = menuPaused;
}

extern "C" __declspec(dllexport) void PG_SetWeatherState(float rain, float snow, float cloud, float fog)
{
    using SB::Proxy::g_report;
    g_report.rain = rain; g_report.snow = snow; g_report.cloud = cloud; g_report.fog = fog;
    g_report.seen = true;
}

extern "C" __declspec(dllexport) void PG_SetWeatherExt(float windX, float windY, float lightning, float gameHour)
{
    using SB::Proxy::g_report;
    g_report.windX = windX; g_report.windY = windY; g_report.lightning = lightning; g_report.gameHour = gameHour;
    g_report.extSeen = true;
}
