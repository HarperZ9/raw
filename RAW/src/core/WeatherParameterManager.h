#pragma once
//=============================================================================
//  WeatherParameterManager — Real-time weather-reactive effect modulation
//
//  Classifies the current TESWeather into one of 9 categories and interpolates
//  per-category multipliers using RE::Sky transition data. Effect renderers
//  query the manager for weather-scaled intensity values each frame.
//
//  Categories: Clear, Cloudy, Foggy, Rain, ThunderRain, Snow, Blizzard, Ash, Special
//
//  Design rationale:
//    - Rain increases SSR (wet surfaces reflect more) and lens dirt
//    - Fog/blizzard reduces AO, SSR, contact shadows (low visibility)
//    - Snow shifts color temperature cooler
//    - Clear weather maximizes all effects
//=============================================================================

#include <cstdint>
#include <array>

namespace SB
{

enum class WeatherCategory : uint8_t
{
    Clear = 0,
    Cloudy,
    Foggy,
    Rain,
    ThunderRain,
    Snow,
    Blizzard,
    Ash,
    Special,
    Count
};

struct WeatherMultipliers
{
    float aoIntensity         = 1.0f;
    float contactShadowStr    = 1.0f;
    float ssrIntensity        = 1.0f;
    float giIntensity         = 1.0f;
    float bloomIntensity      = 1.0f;
    float skylightIntensity   = 1.0f;
    float exposureBias        = 0.0f;  // EV offset
    float saturation          = 1.0f;
    float contrast            = 1.0f;
    float colorTempOffset     = 0.0f;  // Kelvin offset from neutral
    float filmGrainStr        = 0.0f;
    float fogDensityMul       = 1.0f;
};

class WeatherParameterManager
{
public:
    static WeatherParameterManager& Get()
    {
        static WeatherParameterManager inst;
        return inst;
    }

    void Initialize();
    void Update(float deltaTime);

    const WeatherMultipliers& GetCurrent() const { return m_current; }
    WeatherCategory GetCurrentCategory() const { return m_currentCat; }
    WeatherCategory GetPreviousCategory() const { return m_prevCat; }
    float GetTransition() const { return m_transition; }
    bool  IsInitialized() const { return m_initialized; }

private:
    WeatherParameterManager() = default;
    void BuildCategoryTable();
    static WeatherCategory ClassifyWeather(void* tesWeather);
    static WeatherMultipliers Lerp(const WeatherMultipliers& a, const WeatherMultipliers& b, float t);

    bool m_initialized = false;

    std::array<WeatherMultipliers, static_cast<size_t>(WeatherCategory::Count)> m_table;
    WeatherMultipliers m_current;
    WeatherCategory    m_currentCat  = WeatherCategory::Clear;
    WeatherCategory    m_prevCat     = WeatherCategory::Clear;
    float              m_transition  = 1.0f; // 0=fully prev, 1=fully current
};

} // namespace SB
