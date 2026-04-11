#include "WeatherParameterManager.h"
#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>

namespace SB
{

void WeatherParameterManager::Initialize()
{
    BuildCategoryTable();
    m_current = m_table[0]; // Start with Clear
    m_initialized = true;
    SKSE::log::info("WeatherParameterManager: initialized with 9 weather categories");
}

void WeatherParameterManager::BuildCategoryTable()
{
    //                              AO    CSh   SSR   GI    Bloom  Sky   EV     Sat   Con   Temp    Grain  Fog
    // Values from WeatherParams.ini research + artistic tuning
    m_table[0] = { 1.00f, 1.00f, 1.00f, 1.00f, 0.85f, 1.00f,  0.00f, 1.05f, 1.05f,  200.f, 0.08f, 1.0f }; // Clear
    m_table[1] = { 0.90f, 0.90f, 0.80f, 0.90f, 0.75f, 0.85f, -0.05f, 0.95f, 0.95f,    0.f, 0.10f, 1.1f }; // Cloudy
    m_table[2] = { 0.50f, 0.50f, 0.30f, 0.50f, 0.50f, 0.40f, -0.30f, 0.80f, 0.85f, -150.f, 0.15f, 2.0f }; // Foggy
    m_table[3] = { 0.70f, 0.70f, 1.20f, 0.70f, 0.65f, 0.60f, -0.15f, 0.85f, 0.92f, -200.f, 0.12f, 1.3f }; // Rain
    m_table[4] = { 0.60f, 0.60f, 1.10f, 0.60f, 0.70f, 0.50f, -0.10f, 0.80f, 0.90f, -100.f, 0.14f, 1.4f }; // ThunderRain
    m_table[5] = { 0.80f, 0.80f, 0.70f, 0.80f, 0.80f, 0.70f,  0.20f, 0.92f, 0.95f, -400.f, 0.11f, 1.2f }; // Snow
    m_table[6] = { 0.40f, 0.40f, 0.20f, 0.40f, 0.55f, 0.30f,  0.10f, 0.75f, 0.80f, -500.f, 0.18f, 2.5f }; // Blizzard
    m_table[7] = { 0.35f, 0.35f, 0.15f, 0.35f, 0.45f, 0.25f, -0.40f, 0.65f, 0.88f,  300.f, 0.20f, 1.8f }; // Ash
    m_table[8] = { 1.00f, 1.00f, 1.00f, 1.00f, 1.20f, 1.00f,  0.00f, 1.00f, 1.00f,    0.f, 0.05f, 1.0f }; // Special
}

WeatherCategory WeatherParameterManager::ClassifyWeather(void* tesWeather)
{
    if (!tesWeather)
        return WeatherCategory::Clear;

    auto* weather = static_cast<RE::TESWeather*>(tesWeather);
    auto flags = weather->data.flags;

    // Check for specific weather flags (RE::TESWeather::WeatherDataFlag)
    using F = RE::TESWeather::WeatherDataFlag;

    bool pleasant = flags.any(F::kPleasant);
    bool cloudy   = flags.any(F::kCloudy);
    bool rainy    = flags.any(F::kRainy);
    bool snowy    = flags.any(F::kSnow);

    // Classify based on flag combinations
    // Blizzard: snow + cloudy/rainy (severe snow)
    if (snowy && (cloudy || rainy))
        return WeatherCategory::Blizzard;

    if (snowy)
        return WeatherCategory::Snow;

    // Thunder: rainy + special conditions (check precipitation amount)
    if (rainy && cloudy)
        return WeatherCategory::ThunderRain;

    if (rainy)
        return WeatherCategory::Rain;

    if (cloudy)
        return WeatherCategory::Cloudy;

    if (pleasant)
        return WeatherCategory::Clear;

    // Fallback: check for ash/fog by name or other flags
    // Ash weather types typically have specific FormIDs in Solstheim
    // For now, default unknown weathers to Cloudy
    return WeatherCategory::Cloudy;
}

WeatherMultipliers WeatherParameterManager::Lerp(
    const WeatherMultipliers& a, const WeatherMultipliers& b, float t)
{
    WeatherMultipliers r;
    auto mix = [t](float va, float vb) { return va + (vb - va) * t; };
    r.aoIntensity       = mix(a.aoIntensity,       b.aoIntensity);
    r.contactShadowStr  = mix(a.contactShadowStr,  b.contactShadowStr);
    r.ssrIntensity      = mix(a.ssrIntensity,       b.ssrIntensity);
    r.giIntensity       = mix(a.giIntensity,         b.giIntensity);
    r.bloomIntensity    = mix(a.bloomIntensity,       b.bloomIntensity);
    r.skylightIntensity = mix(a.skylightIntensity,   b.skylightIntensity);
    r.exposureBias      = mix(a.exposureBias,         b.exposureBias);
    r.saturation        = mix(a.saturation,           b.saturation);
    r.contrast          = mix(a.contrast,             b.contrast);
    r.colorTempOffset   = mix(a.colorTempOffset,     b.colorTempOffset);
    r.filmGrainStr      = mix(a.filmGrainStr,         b.filmGrainStr);
    r.fogDensityMul     = mix(a.fogDensityMul,       b.fogDensityMul);
    return r;
}

void WeatherParameterManager::Update(float /*deltaTime*/)
{
    auto* sky = RE::Sky::GetSingleton();
    if (!sky) return;

    // Classify current and previous weather
    m_currentCat = ClassifyWeather(sky->currentWeather);
    m_prevCat    = ClassifyWeather(sky->lastWeather);

    // Transition factor: 0 = fully previous, 1 = fully current
    m_transition = sky->currentWeatherPct;

    // Interpolate between previous and current category values
    const auto& prev = m_table[static_cast<size_t>(m_prevCat)];
    const auto& curr = m_table[static_cast<size_t>(m_currentCat)];

    // Use smoothstep for perceptually smoother transitions
    float t = m_transition * m_transition * (3.0f - 2.0f * m_transition);
    m_current = Lerp(prev, curr, t);
}

} // namespace SB
