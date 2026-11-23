// Weather state for the ray-tracing worker's dynamic weather (RAW engine runtime). Original work, MIT. See rt_weather.h.
#include "rt_weather.h"
#include "rt_version.h"
#include "rt_view.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

extern "C" uint64_t RAW_EngineView(raw::rt::EngineView* out);

namespace raw::rt {
namespace {

using GetSkyFn = uint8_t* (*)();
using SetWeatherFn = void (*)(float rain, float snow, float cloud, float fog);
using SetWeatherExtFn = void (*)(float windX, float windY, float lightning, float gameHour);

const Image* g_img = nullptr;
GetSkyFn g_getSky = nullptr;
SetWeatherFn g_set = nullptr;
SetWeatherExtFn g_setExt = nullptr;

struct WeatherRead {
    uint32_t formId = 0;
    uint8_t flags = 0, fadeIn = 0, fadeOut = 0, windSpeed = 0, windDir = 0, lightningFreq = 0;
    float fogFar = 0;
    bool ok = false;
};
struct State {
    WeatherRead cur, last;
    float pct = 0, density = 0, gameHour = 12.0f;
    float rain = 0, snow = 0, cloud = 0, fog = 0;
    float smoothRain = 0, smoothSnow = 0, smoothCloud = 0, smoothFog = 0;
    bool hasSmooth = false;
    uint64_t lastTickMs = 0;
    float windX = 0, windY = 0, lightning = 0;
    float skyUpper[3] = {}, skyLower[3] = {}, skyHoriz[3] = {};
    // Ground-up native synoptic weather simulator telemetry:
    float pressureHpa = 1014.0f, tempC = 6.0f, humidity = 0.55f;
    const char* biome = "temperate";
    bool ok = false;
    uint64_t calls = 0;
};
State g_state;

float Sat(float x) { return std::clamp(x, 0.0f, 1.0f); }

WeatherRead ReadWeather(const uint8_t* w) {
    WeatherRead r;
    if (!w) return r;
    __try {
        r.formId = *reinterpret_cast<const uint32_t*>(w + 0x14);
        r.windSpeed = w[0x664 + 0x00];
        r.fadeIn = w[0x664 + 0x06];
        r.fadeOut = w[0x664 + 0x07];
        r.lightningFreq = w[0x664 + 0x0A];
        r.flags = w[0x664 + 0x0B];
        r.windDir = w[0x664 + 0x0F];
        r.fogFar = *reinterpret_cast<const float*>(w + 0x678 + 0x04);
        r.ok = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        r.ok = false;
    }
    return r;
}

bool ReadSky(State* s) {
    __try {
        const uint8_t* sky = g_getSky();
        if (!sky) return false;
        const auto* cur = *reinterpret_cast<const uint8_t* const*>(sky + 0x48);
        const auto* last = *reinterpret_cast<const uint8_t* const*>(sky + 0x50);
        s->pct = Sat(*reinterpret_cast<const float*>(sky + 0x1B8));
        // Exact verified Sky::currentGameHour offset on 1.6.1170 / 1.7.104.0 is +0x1B0 (matching rt_view.cpp:85;
        // +0x1BC was 12 bytes off and broke 24-hour dawn/sunrise/sunset/dusk/night sky & solar gating!).
        const float gh = *reinterpret_cast<const float*>(sky + 0x1B0);
        if (gh >= 0.0f && gh <= 24.0f) s->gameHour = gh;
        const auto* precip = *reinterpret_cast<const uint8_t* const*>(sky + 0xA0);
        s->density = precip ? *reinterpret_cast<const float*>(precip + 0x84) : 0.0f;
        // Live Sky::skyColor[0..2] (upper sky +0xD8, lower sky +0xE4, horizon +0xF0) probe for verified 1.7.104.0 mapping.
        for (int c = 0; c < 3; ++c) {
            s->skyUpper[c] = *reinterpret_cast<const float*>(sky + 0xD8 + c * 4);
            s->skyLower[c] = *reinterpret_cast<const float*>(sky + 0xE4 + c * 4);
            s->skyHoriz[c] = *reinterpret_cast<const float*>(sky + 0xF0 + c * 4);
        }
        s->cur = ReadWeather(cur);
        s->last = last ? ReadWeather(last) : WeatherRead{};
        return s->cur.ok;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

float FogOf(const WeatherRead& w) {
    if (!w.ok || w.fogFar <= 0) return 0;
    return Sat((150000.0f - w.fogFar) / (150000.0f - 5000.0f));
}

void Blend(State* s) {
    const WeatherRead& c = s->cur;
    const WeatherRead& l = s->last.ok ? s->last : s->cur;
    const float p = s->last.ok ? s->pct : 1.0f;
    // precipitation of the incoming weather starts at its fade-in point; the outgoing one's stops at its fade-out
    const float in = c.fadeIn >= 255 ? 0.0f : Sat((p - c.fadeIn / 255.0f) / (1.0f - c.fadeIn / 255.0f));
    const float out = l.fadeOut == 0 ? 0.0f : Sat(1.0f - p / (l.fadeOut / 255.0f));
    auto has = [](const WeatherRead& w, uint8_t bit) { return (w.flags & bit) ? 1.0f : 0.0f; };
    s->rain = Sat(has(c, 4) * in + has(l, 4) * out);
    s->snow = Sat(has(c, 8) * in + has(l, 8) * out);
    // Graduated physical cloud cover per TESWeather flags (fallback baseline before synoptic modulation):
    auto cover = [&](const WeatherRead& w) {
        if (w.flags & 4) return 0.88f;
        if (w.flags & 8) return 0.82f;
        if (w.flags & 2) return 0.58f;
        return 0.24f;
    };
    const float vanCloud = Sat(cover(c) * p + cover(l) * (1.0f - p));
    const float vanFog = Sat(FogOf(c) * p + FogOf(l) * (1.0f - p));

    // Ground-Up Native Synoptic Weather Engine (replaces Skyrim's static 2011 TESWeather lookup tables):
    // Continuous barometric pressure fronts, regional climate biomes from live player coordinates,
    // diurnal valley mist & convective cumulus cycles, and turbulent wind gusts — while still honoring
    // scripted quest/shout storms (TESWeather flags & 4 or flags & 8).
    EngineView ev{};
    RAW_EngineView(&ev);
    const float wx = (ev.ok & 2u) ? ev.playerPos[0] : 0.0f;
    const float wy = (ev.ok & 2u) ? ev.playerPos[1] : 0.0f;
    const float wz = (ev.ok & 2u) ? ev.playerPos[2] : 0.0f;
    const double simHours = double(ev.daysPassed) * 24.0 + double(s->gameHour);
    const double tReal = double(GetTickCount64() % 3600000ull) * 0.001;
    // Regional climate weights from Tamriel world coordinates:
    const float arcticW = Sat((wy - 22000.0f) / 38000.0f);               // Dawnstar / Winterhold / Windhelm / Sea of Ghosts
    const float alpineW = Sat((wz - 1600.0f) / 3800.0f);                 // High mountain passes / Throat of the World
    const float valleyW = Sat(1.0f - arcticW - alpineW * 0.7f);          // Riverwood / Falkreath / Rift / Whiterun
    s->biome = arcticW > 0.55f ? "arctic" : (alpineW > 0.45f ? "alpine" : (wy < -20000.0f ? "valley_forest" : "tundra"));

    // Multi-wave synoptic barometric pressure field (984..1028 hPa) advected smoothly across Skyrim:
    const double frontA = std::sin(simHours * 0.19 + double(wx) * 0.000011 + double(wy) * 0.000008);
    const double frontB = std::sin(simHours * 0.47 - double(wx) * 0.000017 + 1.9);
    const double frontC = std::cos(simHours * 0.083 + 0.7);
    const float pressure = float(1011.0 + frontA * 12.0 + frontB * 6.0 + frontC * 7.0 - double(arcticW) * 5.0);
    s->pressureHpa = std::clamp(pressure, 982.0f, 1030.0f);
    // Physical diurnal temperature curve (coldest at 04:30, warmest at 14:30; colder in arctic & alpine regions):
    const float diurnalT = float(-std::cos((s->gameHour - 4.5f) * 0.26179939f));
    s->tempC = 8.5f + diurnalT * 6.5f - arcticW * 14.0f - alpineW * 12.0f;
    // Synoptic moisture & diurnal microclimate (morning valley mist 04:30..07:30, afternoon convective cumulus 13:00..17:00):
    const float lowPres = Sat((1012.0f - s->pressureHpa) / 24.0f);
    const float morningMist = float(std::exp(-std::pow((s->gameHour - 5.8f) / 1.45f, 2.0f))) * (0.45f + 0.55f * valleyW);
    const float afternoonConv = float(std::exp(-std::pow((s->gameHour - 15.0f) / 2.6f, 2.0f))) * 0.16f;
    s->humidity = Sat(0.42f + lowPres * 0.48f + morningMist * 0.28f + (s->rain > 0.05f ? 0.30f : 0.0f));

    // Blend synoptic cloud/precip/fog with scripted TESWeather (scripted storms always take priority):
    const float synCloud = Sat(0.18f + lowPres * 0.72f + afternoonConv + arcticW * 0.14f);
    const float synPrecip = Sat((lowPres - 0.58f) / 0.36f);
    const float freezeW = Sat((1.5f - s->tempC) / 4.5f);
    const float synRain = synPrecip * (1.0f - freezeW);
    const float synSnow = synPrecip * freezeW;
    const float synFog = Sat(0.12f + morningMist * 0.42f + lowPres * 0.28f + alpineW * 0.18f);

    s->rain  = std::max(s->rain,  synRain  * 0.85f);
    s->snow  = std::max(s->snow,  synSnow  * 0.85f);
    s->cloud = Sat(std::max(vanCloud, synCloud));
    s->fog   = Sat(vanFog * 0.55f + synFog * 0.45f);

    // Physical weather hysteresis (asymmetric ramp-up / evaporation drying): puddles & wet stone dry over ~45s
    // after rain stops and snowdrifts accumulate/melt smoothly over ~35s instead of snapping on weather transitions.
    const uint64_t nowMs = GetTickCount64();
    const float dt = s->lastTickMs ? std::clamp(float(nowMs - s->lastTickMs) * 0.001f, 0.0f, 0.25f) : 0.0f;
    s->lastTickMs = nowMs;
    if (!s->hasSmooth) {
        s->smoothRain = s->rain; s->smoothSnow = s->snow; s->smoothCloud = s->cloud; s->smoothFog = s->fog;
        s->hasSmooth = true;
    } else if (dt > 0.0f) {
        auto approach = [](float curV, float tgt, float upRate, float downRate, float d) {
            const float rate = tgt > curV ? upRate : downRate;
            return curV + (tgt - curV) * std::min(1.0f, rate * d);
        };
        s->smoothRain  = approach(s->smoothRain,  s->rain,  0.065f, 0.024f, dt);   // ~15s wet-up, ~42s dry-down
        s->smoothSnow  = approach(s->smoothSnow,  s->snow,  0.035f, 0.028f, dt);   // ~28s snowfall, ~35s melt
        s->smoothCloud = approach(s->smoothCloud, s->cloud, 0.14f,  0.14f,  dt);   // smooth ~7s cloud morph
        s->smoothFog   = approach(s->smoothFog,   s->fog,   0.18f,  0.18f,  dt);
    }
    // Engine-native synoptic wind vector + turbulent gust envelope (drives volumetric clouds, water chop & rain slant):
    const float ws = std::max((c.windSpeed * p + l.windSpeed * (1.0f - p)) / 255.0f, lowPres * 0.72f + arcticW * 0.28f);
    const float gust = 1.0f + float(std::sin(tReal * 0.41) * 0.18 + std::sin(tReal * 1.13 + 2.1) * 0.10) * (0.4f + lowPres);
    const float ang = (c.windDir / 255.0f) * 6.2831853f + float(frontB * 0.35);
    const float speed = (55.0f + ws * 380.0f) * gust;
    s->windX = std::cos(ang) * speed;
    s->windY = std::sin(ang) * speed;
    const float cLight = (c.flags & 4) ? ((255.0f - float(c.lightningFreq)) / 255.0f) : 0.0f;
    const float lLight = (l.flags & 4) ? ((255.0f - float(l.lightningFreq)) / 255.0f) : 0.0f;
    const float synLight = (synRain > 0.35f && s->pressureHpa < 996.0f) ? Sat((996.0f - s->pressureHpa) / 14.0f) : 0.0f;
    s->lightning = Sat(std::max(cLight * p + lLight * (1.0f - p), synLight));
}

}  // namespace

void WeatherInit(const Image& img) {
    g_img = &img;
    const uint32_t getSky = Addr("fn:Sky::GetSingleton");
    g_getSky = getSky ? reinterpret_cast<GetSkyFn>(const_cast<uint8_t*>(img.base) + getSky) : nullptr;
    if (HMODULE proxy = GetModuleHandleA("d3d11.dll")) {
        g_set = reinterpret_cast<SetWeatherFn>(GetProcAddress(proxy, "PG_SetWeatherState"));
        g_setExt = reinterpret_cast<SetWeatherExtFn>(GetProcAddress(proxy, "PG_SetWeatherExt"));
    }
}

void WeatherOnPresent() {
    // DirectInput8-proof camera freeze: zero Skyrim's native fMouseHeadingSensitivity:Controls while RAW menu is open.
    if (g_img) {
        using IsMenuOpenFn = int(*)();
        static IsMenuOpenFn s_isMenu = nullptr;
        static bool s_probed = false, s_locked = false;
        static float s_savedSens = 0.0125f;
        if (!s_probed) {
            s_probed = true;
            if (HMODULE proxy = GetModuleHandleA("d3d11.dll"))
                s_isMenu = reinterpret_cast<IsMenuOpenFn>(GetProcAddress(proxy, "PG_IsMenuOpen"));
        }
        const uint32_t sensRva = Addr("fMouseHeadingSensitivity:Controls");
        if (s_isMenu && sensRva) {
            auto* pf = reinterpret_cast<float*>(const_cast<uint8_t*>(g_img->base) + sensRva + 8);
            const bool open = s_isMenu() != 0;
            if (open && !s_locked) {
                s_savedSens = *pf;
                *pf = 0.0f;
                s_locked = true;
            } else if (!open && s_locked) {
                *pf = s_savedSens > 0.0001f ? s_savedSens : 0.0125f;
                s_locked = false;
            }
        }
    }
    if (!g_getSky) return;
    State s = g_state;
    s.ok = ReadSky(&s);
    if (s.ok) Blend(&s);
    if (s.ok && g_set) { g_set(s.smoothRain, s.smoothSnow, s.smoothCloud, s.smoothFog); ++s.calls; }
    if (s.ok && g_setExt) { g_setExt(s.windX, s.windY, s.lightning, s.gameHour); }
    g_state = s;
}

std::string WeatherCommand() {
    const State& s = g_state;
    char b[896];
    std::snprintf(b, sizeof b,
                  "{\"cmd\":\"weather\",\"ok\":%s,\"exported\":%s,\"calls\":%llu,\"rain\":%.3f,\"snow\":%.3f,\"cloud\":%.3f,"
                  "\"fog\":%.3f,\"wind_x\":%.1f,\"wind_y\":%.1f,\"lightning\":%.3f,\"game_hour\":%.2f,\"pct\":%.3f,\"density\":%.3f,"
                  "\"pressure_hpa\":%.1f,\"temp_c\":%.1f,\"humidity\":%.2f,\"biome\":\"%s\","
                  "\"sky_upper\":[%.2f,%.2f,%.2f],\"sky_lower\":[%.2f,%.2f,%.2f],\"sky_horiz\":[%.2f,%.2f,%.2f],"
                  "\"current\":\"0x%08X\",\"current_flags\":%u,\"current_fog_far\":%.0f,\"last\":\"0x%08X\",\"last_flags\":%u}",
                  s.ok ? "true" : "false", g_set ? "true" : "false", (unsigned long long)s.calls, s.rain, s.snow, s.cloud,
                  s.fog, s.windX, s.windY, s.lightning, s.gameHour, s.pct, s.density,
                  s.pressureHpa, s.tempC, s.humidity, s.biome ? s.biome : "temperate",
                  s.skyUpper[0], s.skyUpper[1], s.skyUpper[2], s.skyLower[0], s.skyLower[1], s.skyLower[2],
                  s.skyHoriz[0], s.skyHoriz[1], s.skyHoriz[2],
                  s.cur.formId, s.cur.flags, s.cur.fogFar,
                  s.last.formId, s.last.flags);
    return b;
}

}  // namespace raw::rt
