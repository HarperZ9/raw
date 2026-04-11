#pragma once
//=============================================================================
//  TimeOfDaySystem.h — 6-Period Time-of-Day Weight Interpolation
//
//  Converts the 24-hour game time into 6 weight factors:
//  Dawn, Sunrise, Day, Sunset, Dusk, Night
//  Each weight is 0..1 and they sum to 1.0.
//
//  Time ranges configured from enbseries.ini [TIMEOFDAY] section.
//=============================================================================

struct TimeOfDayConfig
{
    float dawnDuration   = 2.0f;
    float sunriseTime    = 6.0f;
    float dayTime        = 13.0f;
    float sunsetTime     = 20.0f;
    float duskDuration   = 2.0f;
    float nightTime      = 1.0f;
};

struct TimeOfDayWeights
{
    float dawn    = 0.0f;
    float sunrise = 0.0f;
    float day     = 1.0f;
    float sunset  = 0.0f;
    float dusk    = 0.0f;
    float night   = 0.0f;

    // Access as array for interpolation
    const float* AsArray() const { return &dawn; }
    float*       AsArray()       { return &dawn; }
};

class TimeOfDaySystem
{
public:
    void SetConfig(const TimeOfDayConfig& config) { m_config = config; }

    // Compute weights for a given time (0..24 float)
    TimeOfDayWeights Compute(float timeOfDay) const;

    // Compute night/day factor (0 = night, 1 = day)
    float ComputeNightDayFactor(float timeOfDay) const;

private:
    TimeOfDayConfig m_config;

    // Helper: smooth transition weight
    static float SmoothStep(float edge0, float edge1, float x);
};

extern TimeOfDaySystem g_TOD;
