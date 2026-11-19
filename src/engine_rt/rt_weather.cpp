// Weather state for the ray-tracing worker's dynamic weather (RAW engine runtime). Original work, MIT. See rt_weather.h.
#include "rt_weather.h"

#include <windows.h>

#include <algorithm>
#include <cstdio>

namespace raw::rt {
namespace {

using GetSkyFn = uint8_t* (*)();
using SetWeatherFn = void (*)(float rain, float snow, float cloud, float fog);

const Image* g_img = nullptr;
GetSkyFn g_getSky = nullptr;
SetWeatherFn g_set = nullptr;

struct WeatherRead { uint32_t formId = 0; uint8_t flags = 0, fadeIn = 0, fadeOut = 0; float fogFar = 0; bool ok = false; };
struct State {
    WeatherRead cur, last;
    float pct = 0, density = 0;
    float rain = 0, snow = 0, cloud = 0, fog = 0;
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
        r.fadeIn = w[0x664 + 0x06];
        r.fadeOut = w[0x664 + 0x07];
        r.flags = w[0x664 + 0x0B];
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
        const auto* precip = *reinterpret_cast<const uint8_t* const*>(sky + 0xA0);
        s->density = precip ? *reinterpret_cast<const float*>(precip + 0x84) : 0.0f;
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
    // cloud cover: cloudy, rainy and snowy weathers are overcast
    auto cover = [&](const WeatherRead& w) { return (w.flags & (2 | 4 | 8)) ? 1.0f : 0.0f; };
    s->cloud = Sat(cover(c) * p + cover(l) * (1.0f - p));
    s->fog = Sat(FogOf(c) * p + FogOf(l) * (1.0f - p));
}

}  // namespace

void WeatherInit(const Image& img) {
    g_img = &img;
    g_getSky = reinterpret_cast<GetSkyFn>(const_cast<uint8_t*>(img.base) + 0x01C2640);
    if (HMODULE proxy = GetModuleHandleA("d3d11.dll"))
        g_set = reinterpret_cast<SetWeatherFn>(GetProcAddress(proxy, "PG_SetWeatherState"));
}

void WeatherOnPresent() {
    if (!g_getSky) return;
    State s = g_state;
    s.ok = ReadSky(&s);
    if (s.ok) Blend(&s);
    if (s.ok && g_set) { g_set(s.rain, s.snow, s.cloud, s.fog); ++s.calls; }
    g_state = s;
}

std::string WeatherCommand() {
    const State& s = g_state;
    char b[512];
    std::snprintf(b, sizeof b,
                  "{\"cmd\":\"weather\",\"ok\":%s,\"exported\":%s,\"calls\":%llu,\"rain\":%.3f,\"snow\":%.3f,\"cloud\":%.3f,"
                  "\"fog\":%.3f,\"pct\":%.3f,\"density\":%.3f,\"current\":\"0x%08X\",\"current_flags\":%u,\"current_fog_far\":%.0f,"
                  "\"last\":\"0x%08X\",\"last_flags\":%u}",
                  s.ok ? "true" : "false", g_set ? "true" : "false", (unsigned long long)s.calls, s.rain, s.snow, s.cloud,
                  s.fog, s.pct, s.density, s.cur.formId, s.cur.flags, s.cur.fogFar, s.last.formId, s.last.flags);
    return b;
}

}  // namespace raw::rt
