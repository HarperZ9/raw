//=============================================================================
//  TimeOfDaySystem.cpp — 6-Period Time-of-Day Weight Interpolation
//
//  ENB's time-of-day system divides the 24-hour cycle into 6 periods:
//
//    Night -> Dawn -> Sunrise -> Day -> Sunset -> Dusk -> Night
//
//  The transitions are smooth and the weights always sum to 1.0.
//  This matches the original ENB behavior from the string evidence
//  and parameter naming: %sDawn, %sSunrise, %sDay, %sSunset, %sDusk, %sNight.
//=============================================================================

#include "TimeOfDaySystem.h"
#include <algorithm>
#include <cmath>

TimeOfDaySystem g_TOD;

float TimeOfDaySystem::SmoothStep(float edge0, float edge1, float x)
{
    float t = (x - edge0) / (edge1 - edge0);
    t = (std::max)(0.0f, (std::min)(1.0f, t));
    return t * t * (3.0f - 2.0f * t);
}

TimeOfDayWeights TimeOfDaySystem::Compute(float timeOfDay) const
{
    // Wrap to 0..24
    while (timeOfDay < 0.0f)  timeOfDay += 24.0f;
    while (timeOfDay >= 24.0f) timeOfDay -= 24.0f;

    TimeOfDayWeights w = {};

    // Key times (from enbseries.ini [TIMEOFDAY])
    const float dawnDur  = m_config.dawnDuration;
    const float sunrise  = m_config.sunriseTime;       // center of sunrise
    const float sunset   = m_config.sunsetTime;         // center of sunset
    const float duskDur  = m_config.duskDuration;
    const float nightT   = m_config.nightTime;          // transition width

    // Derived boundaries
    const float dawnStart    = sunrise - dawnDur;
    const float dawnEnd      = sunrise;
    const float sunriseStart = sunrise;
    const float sunriseEnd   = sunrise + dawnDur;
    const float dayStart     = sunriseEnd;
    const float dayEnd       = sunset - duskDur;
    const float sunsetStart  = sunset - duskDur;
    const float sunsetEnd    = sunset;
    const float duskStart    = sunset;
    const float duskEnd      = sunset + duskDur;

    // Use transition width for smooth blending (nightT parameter)
    const float halfTrans = (std::max)(nightT * 0.5f, 0.25f);

    float t = timeOfDay;

    // Compute each weight as overlap with its time window
    // Dawn: dawnStart to dawnEnd
    if (t >= dawnStart - halfTrans && t <= dawnEnd + halfTrans)
    {
        float rise = SmoothStep(dawnStart - halfTrans, dawnStart + halfTrans, t);
        float fall = 1.0f - SmoothStep(dawnEnd - halfTrans, dawnEnd + halfTrans, t);
        w.dawn = rise * fall;
    }

    // Sunrise: sunriseStart to sunriseEnd
    if (t >= sunriseStart - halfTrans && t <= sunriseEnd + halfTrans)
    {
        float rise = SmoothStep(sunriseStart - halfTrans, sunriseStart + halfTrans, t);
        float fall = 1.0f - SmoothStep(sunriseEnd - halfTrans, sunriseEnd + halfTrans, t);
        w.sunrise = rise * fall;
    }

    // Day: dayStart to dayEnd
    if (t >= dayStart - halfTrans && t <= dayEnd + halfTrans)
    {
        float rise = SmoothStep(dayStart - halfTrans, dayStart + halfTrans, t);
        float fall = 1.0f - SmoothStep(dayEnd - halfTrans, dayEnd + halfTrans, t);
        w.day = rise * fall;
    }

    // Sunset: sunsetStart to sunsetEnd
    if (t >= sunsetStart - halfTrans && t <= sunsetEnd + halfTrans)
    {
        float rise = SmoothStep(sunsetStart - halfTrans, sunsetStart + halfTrans, t);
        float fall = 1.0f - SmoothStep(sunsetEnd - halfTrans, sunsetEnd + halfTrans, t);
        w.sunset = rise * fall;
    }

    // Dusk: duskStart to duskEnd
    if (t >= duskStart - halfTrans && t <= duskEnd + halfTrans)
    {
        float rise = SmoothStep(duskStart - halfTrans, duskStart + halfTrans, t);
        float fall = 1.0f - SmoothStep(duskEnd - halfTrans, duskEnd + halfTrans, t);
        w.dusk = rise * fall;
    }

    // Night: everything else (complement)
    float sum = w.dawn + w.sunrise + w.day + w.sunset + w.dusk;
    w.night = (std::max)(0.0f, 1.0f - sum);

    // Normalize to ensure they sum to 1.0
    float total = w.dawn + w.sunrise + w.day + w.sunset + w.dusk + w.night;
    if (total > 0.0001f)
    {
        float inv = 1.0f / total;
        w.dawn    *= inv;
        w.sunrise *= inv;
        w.day     *= inv;
        w.sunset  *= inv;
        w.dusk    *= inv;
        w.night   *= inv;
    }
    else
    {
        w.night = 1.0f; // fallback
    }

    return w;
}

float TimeOfDaySystem::ComputeNightDayFactor(float timeOfDay) const
{
    TimeOfDayWeights w = Compute(timeOfDay);
    // Day factor: 1.0 during day, 0.0 during night
    // Sunrise/sunset contribute partially
    return w.sunrise * 0.5f + w.day * 1.0f + w.sunset * 0.5f + w.dawn * 0.25f + w.dusk * 0.25f;
}
