#include "CelestialTracker.h"
#include "Projection.h"
#include <RE/Skyrim.h>
#include <cmath>

namespace SB::CelestialTracker
{
    // ── Calculate moon phase from game day ──────────────────────────────
    // Skyrim's moons have different cycles. We approximate using game day.
    // Masser: 24-day cycle (8 phases, 3 days each)
    // Secunda: Could use a different cycle, but we'll use a simple approximation.
    static RE::Moon::Phase GetMoonPhase(bool a_isMasser)
    {
        auto* calendar = RE::Calendar::GetSingleton();
        if (!calendar)
            return RE::Moon::Phase::kFull;

        // Get current game day (days since game start)
        float gameDay = calendar->GetDaysPassed();
        int dayInt = static_cast<int>(gameDay);

        // Masser: 24-day cycle (8 phases)
        // Secunda: offset by half cycle for variety
        int offset = a_isMasser ? 0 : 4;
        int phase = (dayInt + offset) % 8;

        // Map to Phase enum
        switch (phase) {
        case 0:  return RE::Moon::Phase::kFull;
        case 1:  return RE::Moon::Phase::kWaningGibbous;
        case 2:  return RE::Moon::Phase::kWaningQuarter;
        case 3:  return RE::Moon::Phase::kWaningCrescent;
        case 4:  return RE::Moon::Phase::kNewMoon;
        case 5:  return RE::Moon::Phase::kWaxingCrescent;
        case 6:  return RE::Moon::Phase::kWaxingQuarter;
        case 7:  return RE::Moon::Phase::kWaxingGibbous;
        default: return RE::Moon::Phase::kFull;
        }
    }

    // ── Moon phase to brightness [0,1] ──────────────────────────────────
    static float PhaseBrightness(bool a_isMasser)
    {
        using P = RE::Moon::Phase;
        switch (GetMoonPhase(a_isMasser)) {
        case P::kFull:            return 1.00f;
        case P::kWaningGibbous:
        case P::kWaxingGibbous:   return 0.75f;
        case P::kWaningQuarter:
        case P::kWaxingQuarter:   return 0.50f;
        case P::kWaxingCrescent:
        case P::kWaningCrescent:  return 0.25f;
        case P::kNewMoon:         return 0.00f;
        default:                  return 0.50f;
        }
    }

    // ── Extract position from a SkyObject's NiNode ──────────────────────
    static bool GetSkyObjectPos(const RE::SkyObject* a_obj, RE::NiPoint3& a_out)
    {
        if (!a_obj || !a_obj->root)
            return false;

        // Check if the object is culled (hidden)
        if (a_obj->root->GetAppCulled())
            return false;

        a_out = a_obj->root.get()->world.translate;
        return true;
    }

    // ── Main update ─────────────────────────────────────────────────────
    CelestialData Update()
    {
        CelestialData data{};

        auto* sky = RE::Sky::GetSingleton();
        if (!sky)
            return data;

        // ── Sun ─────────────────────────────────────────────────────────
        RE::NiPoint3 sunPos{};
        if (sky->sun && GetSkyObjectPos(sky->sun, sunPos)) {
            data.SunNDC       = Projection::WorldToNDC(sunPos);
            auto dir          = Projection::DirectionAndElevation(sunPos);
            data.SunDirection = dir;
            data.SunNDC.w     = dir.w;  // pack elevation into NDC.w too
        }

        // Sun color from current weather (will be refined by AtmosphereTracker)
        if (sky->currentWeather) {
            auto* w = sky->currentWeather;
            // data.sunGlare
            data.SunColor.w = w->data.sunGlare;
        }

        // ── Masser ──────────────────────────────────────────────────────
        RE::NiPoint3 masserPos{};
        if (sky->masser && GetSkyObjectPos(sky->masser, masserPos)) {
            data.MasserNDC       = Projection::WorldToNDC(masserPos);
            data.MasserDirection = Projection::DirectionAndElevation(masserPos);
            data.MasserNDC.w     = PhaseBrightness(true);  // true = Masser
        }

        // ── Secunda ─────────────────────────────────────────────────────
        RE::NiPoint3 secundaPos{};
        if (sky->secunda && GetSkyObjectPos(sky->secunda, secundaPos)) {
            data.SecundaNDC       = Projection::WorldToNDC(secundaPos);
            data.SecundaDirection = Projection::DirectionAndElevation(secundaPos);
            data.SecundaNDC.w     = PhaseBrightness(false);  // false = Secunda
        }

        // ── Time ────────────────────────────────────────────────────────
        auto* calendar = RE::Calendar::GetSingleton();
        if (calendar) {
            float hour = calendar->GetHour();
            data.TimeData.x = hour;
            data.TimeData.w = hour / 24.0f;  // day progress [0,1]
        }

        // Sunrise/sunset from climate
        if (sky->currentClimate) {
            auto* climate = sky->currentClimate;
            // TESClimate timing data is in the Timing struct.
            // sunrise/sunset begin/end are packed as hours.
            auto& timing = climate->timing;
            // timing.sunrise.begin/end, timing.sunset.begin/end
            // These are uint8_t values representing 10-minute intervals (0-143).
            // Convert: hour = value * 10 / 60 = value / 6.0
            data.TimeData.y = static_cast<float>(timing.sunrise.begin) / 6.0f;
            data.TimeData.z = static_cast<float>(timing.sunset.end) / 6.0f;
        }

        return data;
    }
}
