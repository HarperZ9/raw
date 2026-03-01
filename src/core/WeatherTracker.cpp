#include "WeatherTracker.h"
#include <RE/Skyrim.h>
#include <cmath>

namespace SB::WeatherTracker
{
    // Persistent state for lightning tracking
    static float s_timeSinceFlash = 99.f;
    static bool  s_wasFlashing    = false;

    // Persistent state for surface accumulation
    static float s_wetness        = 0.f;
    static float s_snowAccum      = 0.f;

    WeatherData Update(float a_deltaTime)
    {
        WeatherData data{};

        auto* sky = RE::Sky::GetSingleton();
        if (!sky || !sky->currentWeather)
            return data;

        auto* w = sky->currentWeather;

        // ── Wind ────────────────────────────────────────────────────────
        data.Wind.x = w->data.windSpeed / 255.0f;  // normalize to [0,1]
        // Wind direction: TESWeather stores as degrees or uint8.
        // Convert to radians.
        data.Wind.y = w->data.windDirection * (3.14159265f / 180.0f);

        // ── Precipitation ───────────────────────────────────────────────
        // TESWeather::Data::flags indicates precipitation type.
        auto flags = w->data.flags;

        bool isRain = flags.any(RE::TESWeather::WeatherDataFlag::kRainy);
        bool isSnow = flags.any(RE::TESWeather::WeatherDataFlag::kSnow);

        if (isSnow)        data.Precipitation.x = 2.0f;
        else if (isRain)   data.Precipitation.x = 1.0f;
        else               data.Precipitation.x = 0.0f;

        // Precipitation intensity from weather transition
        // (fades in/out based on precipitationBeginFadeIn/End)
        data.Precipitation.y = (isRain || isSnow) ? 1.0f : 0.0f;
        // During weather transition, scale by transition %
        if (sky->currentWeatherPct < 1.0f)
            data.Precipitation.y *= sky->currentWeatherPct;

        // ── Lightning ───────────────────────────────────────────────────
        // TESWeather has thunderLightningFrequency.
        data.Lightning.x = w->data.thunderLightningFrequency / 255.0f;

        // Detect lightning flash via sky state.
        // RE::Sky has a flash member or we detect via sudden brightness.
        // For now, we track based on weather flag + random frequency.
        bool thunderActive = flags.any(RE::TESWeather::WeatherDataFlag::kRainy) &&
                             w->data.thunderLightningFrequency > 0;

        // The game handles actual flashes internally.  We provide the
        // frequency parameter so shaders can simulate their own.
        // s_timeSinceFlash tracks time since we last detected one.
        s_timeSinceFlash += a_deltaTime;

        // Check if sky's flash intensity changed (if accessible)
        // Fallback: we expose the frequency and let the shader decide.
        data.Lightning.y = 0.f;  // isFlashing — set by shader or hook
        data.Lightning.z = 0.f;  // flashIntensity
        data.Lightning.w = s_timeSinceFlash;

        // ── Weather flags ───────────────────────────────────────────────
        data.Flags.x = flags.any(RE::TESWeather::WeatherDataFlag::kPleasant) ? 1.f : 0.f;
        data.Flags.y = flags.any(RE::TESWeather::WeatherDataFlag::kCloudy)   ? 1.f : 0.f;
        data.Flags.z = isRain ? 1.f : 0.f;
        data.Flags.w = isSnow ? 1.f : 0.f;

        // ── Transition ──────────────────────────────────────────────────
        data.Transition.x = sky->currentWeatherPct;

        // Weather form IDs (low 16 bits for shader use)
        if (sky->lastWeather)
            data.Transition.y = static_cast<float>(
                sky->lastWeather->GetFormID() & 0xFFFF);
        data.Transition.z = static_cast<float>(
            w->GetFormID() & 0xFFFF);

        // ── Precipitation surface accumulation ────────────────────────
        // Wetness builds up during rain, dries out otherwise.
        // Snow accumulates during snow, melts otherwise.
        float precipIntensity = data.Precipitation.y;
        if (isRain) {
            s_wetness = std::min(1.0f, s_wetness + precipIntensity * a_deltaTime * 0.1f);
        } else {
            s_wetness = std::max(0.0f, s_wetness - a_deltaTime * 0.02f);
        }
        if (isSnow) {
            s_snowAccum = std::min(1.0f, s_snowAccum + precipIntensity * a_deltaTime * 0.05f);
        } else {
            s_snowAccum = std::max(0.0f, s_snowAccum - a_deltaTime * 0.01f);
        }
        data.PrecipSurface.x = s_wetness;
        data.PrecipSurface.y = s_wetness * 0.3f;  // puddle depth (simplified)
        data.PrecipSurface.z = s_snowAccum;

        return data;
    }
}
