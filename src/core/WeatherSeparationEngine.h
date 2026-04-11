#pragma once
//=============================================================================
//  WeatherSeparationEngine.h — Annotation-driven per-weather parameter values
//
//  Extends the ShaderPreProcessor's Separation annotation to provide
//  per-weather, per-ToD parameter overrides via .fx.ini files.
//
//  When a shader variable has Separation="ExteriorWeather" or "Weather",
//  this engine:
//    1. Loads per-weather values from <shader>.fx.ini files
//    2. Interpolates between current/previous weather values
//    3. Supports 4-slot and 6-slot time-of-day
//    4. Pushes overridden values via ENBSetParameter
//
//  Replaces ENB Extender's weather separation system.
//
//  Author: Zain Dana Harper
//  License: MIT
//=============================================================================

#include "BridgeData.h"
#include "ShaderPreProcessor.h"

#include <string>
#include <unordered_map>
#include <vector>
#include <filesystem>
#include <mutex>

namespace SB
{

// ── Time-of-Day slot definitions ─────────────────────────────────────

enum class ToDSlotMode : uint8_t
{
    FourSlot = 0,   // ENB native: morning, day, sunset, night
    SixSlot  = 1    // SB exclusive: dawn, sunrise, day, sunset, dusk, night
};

// 4-slot: matches ENB's native ToD system
enum class ToD4 : uint8_t { Morning = 0, Day, Sunset, Night, COUNT };

// 6-slot: extended ToD for finer control
enum class ToD6 : uint8_t { Dawn = 0, Sunrise, Day, Sunset, Dusk, Night, COUNT };

static constexpr int kToD4Count = static_cast<int>(ToD4::COUNT);
static constexpr int kToD6Count = static_cast<int>(ToD6::COUNT);

inline const char* ToD4Name(ToD4 slot)
{
    switch (slot) {
    case ToD4::Morning: return "Morning";
    case ToD4::Day:     return "Day";
    case ToD4::Sunset:  return "Sunset";
    case ToD4::Night:   return "Night";
    default:            return "?";
    }
}

inline const char* ToD6Name(ToD6 slot)
{
    switch (slot) {
    case ToD6::Dawn:    return "Dawn";
    case ToD6::Sunrise: return "Sunrise";
    case ToD6::Day:     return "Day";
    case ToD6::Sunset:  return "Sunset";
    case ToD6::Dusk:    return "Dusk";
    case ToD6::Night:   return "Night";
    default:            return "?";
    }
}

// ── Per-weather parameter value ──────────────────────────────────────

struct SeparatedValue
{
    // Key
    std::string paramKey;     // ParameterMeta::GetUniqueKey()
    std::string shaderFile;   // Which .fx shader

    // 4-slot values (one per ToD)
    float values4[kToD4Count] = {};

    // 6-slot values (one per ToD) — used when mode is SixSlot
    float values6[kToD6Count] = {};

    // Which mode is active
    ToDSlotMode slotMode = ToDSlotMode::FourSlot;

    // Current interpolated value
    float currentValue = 0.f;
};

// ── Per-weather INI data ─────────────────────────────────────────────

struct WeatherINI
{
    uint32_t weatherFormID = 0;
    std::unordered_map<std::string, SeparatedValue> params;  // paramKey → values
    bool dirty = false;
};

// ── Weather Separation Engine ────────────────────────────────────────

class WeatherSeparationEngine
{
public:
    static WeatherSeparationEngine& Get();

    // Initialize: scan for separated parameters, load INIs
    void Initialize(const std::filesystem::path& configDir);

    // Per-frame: compute interpolated values, push to ENB
    void Update(float gameHour, float transitionPct,
                uint32_t currentWeatherID, uint32_t prevWeatherID,
                bool isExterior);

    // Save current weather's INI (call on weather change or periodic)
    void SaveWeatherINI(uint32_t weatherFormID);

    // Set a separated parameter's value for the current weather+ToD
    void SetValue(const std::string& paramKey, float value);

    // Get current interpolated value
    float GetValue(const std::string& paramKey) const;

    // Stats
    int GetSeparatedParamCount() const { return m_separatedCount; }
    int GetLoadedWeatherCount() const;
    ToDSlotMode GetSlotMode() const { return m_slotMode; }
    void SetSlotMode(ToDSlotMode mode) { m_slotMode = mode; }

private:
    // Load/save .fx.ini for a specific weather
    WeatherINI& GetOrLoadWeatherINI(uint32_t weatherFormID);
    void LoadWeatherINI(uint32_t weatherFormID, WeatherINI& out);
    void WriteWeatherINI(uint32_t weatherFormID, const WeatherINI& ini);

    // Map game hour to ToD slot + blend factor
    static void ResolveToD4(float gameHour, ToD4& slotA, ToD4& slotB, float& blend);
    static void ResolveToD6(float gameHour, ToD6& slotA, ToD6& slotB, float& blend);

    // Compute value for a separated param at given weather + time
    float ComputeValue(const SeparatedValue& sv, float gameHour) const;

    // Save all dirty INIs
    void SaveAllDirty();

    // Config
    std::filesystem::path m_configDir;
    ToDSlotMode m_slotMode = ToDSlotMode::FourSlot;

    // Weather change tracking for auto-save
    uint32_t m_lastCurrentWeatherID = 0;
    int m_saveCountdown = 0;  // frames until next auto-save check

    // Per-weather INI cache (weatherFormID → INI data)
    std::unordered_map<uint32_t, WeatherINI> m_weatherINIs;

    // Separated parameters from AnnotationDatabase
    std::vector<const ParameterMeta*> m_separatedParams;
    int m_separatedCount = 0;

    // Current interpolated values (paramKey → value)
    std::unordered_map<std::string, float> m_currentValues;

    mutable std::mutex m_mutex;
};

} // namespace SB
