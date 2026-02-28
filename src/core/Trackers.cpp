//=============================================================================
//  Trackers.cpp — Combined tracker implementations
//
//  Contains: CelestialTracker, AtmosphereTracker, FogTracker, WeatherTracker,
//            PlayerTracker, CameraTracker, InteriorTracker, ShadowTracker,
//            EffectsTracker, RenderTracker
//=============================================================================

#include "Trackers.h"
#include "Projection.h"
#include <RE/Skyrim.h>
#include <cmath>
#include <cstring>

//=============================================================================
// CelestialTracker
//=============================================================================
namespace SB::CelestialTracker
{
    // Calculate moon phase from game day
    // Masser: 24-day cycle (8 phases, 3 days each)
    static RE::Moon::Phase GetMoonPhase(bool a_isMasser)
    {
        auto* calendar = RE::Calendar::GetSingleton();
        if (!calendar)
            return RE::Moon::Phase::kFull;

        float gameDay = calendar->GetDaysPassed();
        int dayInt = static_cast<int>(gameDay);

        int offset = a_isMasser ? 0 : 4;
        int phase = (dayInt + offset) % 8;

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

    static bool GetSkyObjectPos(const RE::SkyObject* a_obj, RE::NiPoint3& a_out)
    {
        if (!a_obj || !a_obj->root)
            return false;

        if (a_obj->root->GetAppCulled())
            return false;

        a_out = a_obj->root.get()->world.translate;
        return true;
    }

    CelestialData Update()
    {
        CelestialData data{};

        auto* sky = RE::Sky::GetSingleton();
        if (!sky)
            return data;

        // Sun
        RE::NiPoint3 sunPos{};
        if (sky->sun && GetSkyObjectPos(sky->sun, sunPos)) {
            data.SunNDC       = Projection::WorldToNDC(sunPos);
            auto dir          = Projection::DirectionAndElevation(sunPos);
            data.SunDirection = dir;
            data.SunNDC.w     = dir.w;
        }

        if (sky->currentWeather) {
            auto* w = sky->currentWeather;
            data.SunColor.w = w->data.sunGlare;

            // Sunlight color from weather color data (interpolated by time of day)
            float hour = 12.0f;
            if (auto* cal = RE::Calendar::GetSingleton())
                hour = cal->GetHour();

            // Read sunlight color type from weather data (4 time-of-day slots)
            auto getSlot = [&](int idx) -> Float4 {
                idx &= 3;
                const auto& c = w->colorData[RE::TESWeather::ColorTypes::kSunlight][idx];
                return { c.red / 255.0f, c.green / 255.0f, c.blue / 255.0f, 0.f };
            };

            int slotA, slotB;
            float t;
            if      (hour < 3.f)  { slotA = 3; slotB = 3; t = 0.f; }
            else if (hour < 9.f)  { slotA = 3; slotB = 0; t = (hour - 3.f) / 6.f; }
            else if (hour < 12.f) { slotA = 0; slotB = 1; t = (hour - 9.f) / 3.f; }
            else if (hour < 15.f) { slotA = 1; slotB = 1; t = 0.f; }
            else if (hour < 18.f) { slotA = 1; slotB = 2; t = (hour - 15.f) / 3.f; }
            else if (hour < 21.f) { slotA = 2; slotB = 3; t = (hour - 18.f) / 3.f; }
            else                  { slotA = 3; slotB = 3; t = 0.f; }

            auto cA = getSlot(slotA);
            auto cB = getSlot(slotB);
            data.SunColor.x = cA.x + (cB.x - cA.x) * t;
            data.SunColor.y = cA.y + (cB.y - cA.y) * t;
            data.SunColor.z = cA.z + (cB.z - cA.z) * t;
        }

        // Masser
        RE::NiPoint3 masserPos{};
        if (sky->masser && GetSkyObjectPos(sky->masser, masserPos)) {
            data.MasserNDC       = Projection::WorldToNDC(masserPos);
            data.MasserDirection = Projection::DirectionAndElevation(masserPos);
            data.MasserNDC.w     = PhaseBrightness(true);
        }

        // Secunda
        RE::NiPoint3 secundaPos{};
        if (sky->secunda && GetSkyObjectPos(sky->secunda, secundaPos)) {
            data.SecundaNDC       = Projection::WorldToNDC(secundaPos);
            data.SecundaDirection = Projection::DirectionAndElevation(secundaPos);
            data.SecundaNDC.w     = PhaseBrightness(false);
        }

        // Time
        auto* calendar = RE::Calendar::GetSingleton();
        if (calendar) {
            float hour = calendar->GetHour();
            data.TimeData.x = hour;
            data.TimeData.w = hour / 24.0f;
        }

        if (sky->currentClimate) {
            auto* climate = sky->currentClimate;
            auto& timing = climate->timing;
            data.TimeData.y = static_cast<float>(timing.sunrise.begin) / 6.0f;
            data.TimeData.z = static_cast<float>(timing.sunset.end) / 6.0f;
        }

        return data;
    }
}

//=============================================================================
// AtmosphereTracker
//=============================================================================
namespace SB::AtmosphereTracker
{
    static Float4 GetInterpolatedColor(
        const RE::TESWeather* a_weather,
        int a_colorType,
        float a_hour)
    {
        if (!a_weather)
            return {};

        auto getSlot = [&](int idx) -> Float4 {
            idx = idx & 3;
            const auto& c = a_weather->colorData[a_colorType][idx];
            return {
                c.red   / 255.0f,
                c.green / 255.0f,
                c.blue  / 255.0f,
                c.alpha / 255.0f
            };
        };

        int slotA, slotB;
        float t;

        if (a_hour < 3.0f) {
            slotA = 3; slotB = 3; t = 0.f;
        } else if (a_hour < 9.0f) {
            slotA = 3; slotB = 0; t = (a_hour - 3.0f) / 6.0f;
        } else if (a_hour < 12.0f) {
            slotA = 0; slotB = 1; t = (a_hour - 9.0f) / 3.0f;
        } else if (a_hour < 15.0f) {
            slotA = 1; slotB = 1; t = 0.f;
        } else if (a_hour < 18.0f) {
            slotA = 1; slotB = 2; t = (a_hour - 15.0f) / 3.0f;
        } else if (a_hour < 21.0f) {
            slotA = 2; slotB = 3; t = (a_hour - 18.0f) / 3.0f;
        } else {
            slotA = 3; slotB = 3; t = 0.f;
        }

        auto cA = getSlot(slotA);
        auto cB = getSlot(slotB);

        return {
            cA.x + (cB.x - cA.x) * t,
            cA.y + (cB.y - cA.y) * t,
            cA.z + (cB.z - cA.z) * t,
            cA.w + (cB.w - cA.w) * t,
        };
    }

    static Float4 BlendWeatherColor(
        const RE::TESWeather* a_current,
        const RE::TESWeather* a_last,
        int a_colorType,
        float a_hour,
        float a_transition)
    {
        auto cur = GetInterpolatedColor(a_current, a_colorType, a_hour);

        if (!a_last || a_transition >= 1.0f)
            return cur;

        auto prev = GetInterpolatedColor(a_last, a_colorType, a_hour);
        float t = a_transition;

        return {
            prev.x + (cur.x - prev.x) * t,
            prev.y + (cur.y - prev.y) * t,
            prev.z + (cur.z - prev.z) * t,
            prev.w + (cur.w - prev.w) * t,
        };
    }

    AtmosphereData Update()
    {
        AtmosphereData data{};

        auto* sky = RE::Sky::GetSingleton();
        if (!sky || !sky->currentWeather)
            return data;

        float hour = 12.0f;
        if (auto* cal = RE::Calendar::GetSingleton())
            hour = cal->GetHour();

        float transition = sky->currentWeatherPct;
        auto* current = sky->currentWeather;
        auto* last    = sky->lastWeather;

        using CT = RE::TESWeather::ColorTypes;

        data.SkyUpper        = BlendWeatherColor(current, last, CT::kSkyUpper,        hour, transition);
        data.SkyLower        = BlendWeatherColor(current, last, CT::kSkyLower,        hour, transition);
        data.Horizon         = BlendWeatherColor(current, last, CT::kHorizon,         hour, transition);
        data.Ambient         = BlendWeatherColor(current, last, CT::kAmbient,         hour, transition);
        data.SunlightColor   = BlendWeatherColor(current, last, CT::kSunlight,        hour, transition);
        data.CloudLODDiffuse = BlendWeatherColor(current, last, CT::kCloudLODDiffuse, hour, transition);
        data.CloudLODAmbient = BlendWeatherColor(current, last, CT::kCloudLODAmbient, hour, transition);
        data.EffectLighting  = BlendWeatherColor(current, last, CT::kEffectLighting,  hour, transition);

        data.SunlightColor.w = current->data.sunGlare;

        return data;
    }
}

//=============================================================================
// FogTracker
//=============================================================================
namespace SB::FogTracker
{
    static Float4 GetFogColor(const RE::TESWeather* a_weather, int a_colorType, float a_hour)
    {
        if (!a_weather) return {};

        auto getSlot = [&](int idx) -> Float4 {
            idx &= 3;
            const auto& c = a_weather->colorData[a_colorType][idx];
            return {
                c.red   / 255.0f,
                c.green / 255.0f,
                c.blue  / 255.0f,
                0.f
            };
        };

        int slotA, slotB;
        float t;
        if (a_hour < 3.f)       { slotA = 3; slotB = 3; t = 0.f; }
        else if (a_hour < 9.f)  { slotA = 3; slotB = 0; t = (a_hour - 3.f) / 6.f; }
        else if (a_hour < 12.f) { slotA = 0; slotB = 1; t = (a_hour - 9.f) / 3.f; }
        else if (a_hour < 15.f) { slotA = 1; slotB = 1; t = 0.f; }
        else if (a_hour < 18.f) { slotA = 1; slotB = 2; t = (a_hour - 15.f) / 3.f; }
        else if (a_hour < 21.f) { slotA = 2; slotB = 3; t = (a_hour - 18.f) / 3.f; }
        else                    { slotA = 3; slotB = 3; t = 0.f; }

        auto cA = getSlot(slotA);
        auto cB = getSlot(slotB);
        return {
            cA.x + (cB.x - cA.x) * t,
            cA.y + (cB.y - cA.y) * t,
            cA.z + (cB.z - cA.z) * t,
            0.f
        };
    }

    struct FogDistances {
        float near_ = 0.f, far_ = 0.f, power = 1.f, max_ = 1.f;
    };

    static FogDistances InterpolateFogDistances(const RE::TESWeather* a_weather, float a_nightFactor)
    {
        if (!a_weather) return {};

        const auto& fd = a_weather->fogData;
        float dayT = 1.0f - a_nightFactor;
        float nightT = a_nightFactor;

        return {
            fd.dayNear  * dayT + fd.nightNear  * nightT,
            fd.dayFar   * dayT + fd.nightFar   * nightT,
            fd.dayPower * dayT + fd.nightPower * nightT,
            fd.dayMax   * dayT + fd.nightMax   * nightT,
        };
    }

    FogData Update()
    {
        FogData data{};

        auto* sky = RE::Sky::GetSingleton();
        if (!sky || !sky->currentWeather)
            return data;

        float hour = 12.f;
        if (auto* cal = RE::Calendar::GetSingleton())
            hour = cal->GetHour();

        float nightFactor = 0.f;
        if (hour < 6.f)       nightFactor = 1.f;
        else if (hour < 8.f)  nightFactor = 1.f - (hour - 6.f) / 2.f;
        else if (hour < 18.f) nightFactor = 0.f;
        else if (hour < 20.f) nightFactor = (hour - 18.f) / 2.f;
        else                  nightFactor = 1.f;

        float transition = sky->currentWeatherPct;
        auto* current = sky->currentWeather;
        auto* last    = sky->lastWeather;

        using CT = RE::TESWeather::ColorTypes;

        auto nearCur = GetFogColor(current, CT::kFogNear, hour);
        auto farCur  = GetFogColor(current, CT::kFogFar,  hour);

        if (last && transition < 1.0f) {
            auto nearPrev = GetFogColor(last, CT::kFogNear, hour);
            auto farPrev  = GetFogColor(last, CT::kFogFar,  hour);
            float t = transition;

            nearCur = { nearPrev.x + (nearCur.x - nearPrev.x) * t,
                        nearPrev.y + (nearCur.y - nearPrev.y) * t,
                        nearPrev.z + (nearCur.z - nearPrev.z) * t, 0.f };
            farCur  = { farPrev.x + (farCur.x - farPrev.x) * t,
                        farPrev.y + (farCur.y - farPrev.y) * t,
                        farPrev.z + (farCur.z - farPrev.z) * t, 0.f };
        }

        auto distCur = InterpolateFogDistances(current, nightFactor);

        if (last && transition < 1.0f) {
            auto distPrev = InterpolateFogDistances(last, nightFactor);
            float t = transition;
            distCur.near_ = distPrev.near_ + (distCur.near_ - distPrev.near_) * t;
            distCur.far_  = distPrev.far_  + (distCur.far_  - distPrev.far_)  * t;
            distCur.power = distPrev.power + (distCur.power - distPrev.power) * t;
            distCur.max_  = distPrev.max_  + (distCur.max_  - distPrev.max_)  * t;
        }

        data.NearColor = nearCur;
        data.NearColor.w = distCur.near_;
        data.FarColor  = farCur;
        data.FarColor.w  = distCur.far_;
        data.Density   = { distCur.power, distCur.max_, 0.f, 0.f };

        float waterZ = 0.f;
        float playerZ = 0.f;

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (player) {
            playerZ = player->GetPosition().z;

            auto* cell = player->GetParentCell();
            if (cell) {
                waterZ = cell->GetExteriorWaterHeight();
                data.Density.z = cell->IsInteriorCell() ? 1.f : 0.f;
            }
        }

        data.HeightFog = {
            waterZ,
            playerZ,
            1.0f,
            0.002f
        };

        return data;
    }
}

//=============================================================================
// WeatherTracker
//=============================================================================
namespace SB::WeatherTracker
{
    static float s_timeSinceFlash = 99.f;
    static bool  s_wasFlashing    = false;
    static float s_flashCooldown  = 0.f;
    static float s_flashIntensity = 0.f;

    WeatherData Update(float a_deltaTime)
    {
        WeatherData data{};

        auto* sky = RE::Sky::GetSingleton();
        if (!sky || !sky->currentWeather)
            return data;

        auto* w = sky->currentWeather;

        // Wind
        data.Wind.x = w->data.windSpeed / 255.0f;
        // windDirection is 0-255 byte mapping to 0-2π radians (full circle)
        data.Wind.y = (w->data.windDirection / 255.0f) * (2.0f * 3.14159265f);

        // Precipitation
        auto flags = w->data.flags;

        bool isRain = flags.any(RE::TESWeather::WeatherDataFlag::kRainy);
        bool isSnow = flags.any(RE::TESWeather::WeatherDataFlag::kSnow);

        if (isSnow)        data.Precipitation.x = 2.0f;
        else if (isRain)   data.Precipitation.x = 1.0f;
        else               data.Precipitation.x = 0.0f;

        data.Precipitation.y = (isRain || isSnow) ? 1.0f : 0.0f;
        if (sky->currentWeatherPct < 1.0f)
            data.Precipitation.y *= sky->currentWeatherPct;

        // Lightning
        data.Lightning.x = w->data.thunderLightningFrequency / 255.0f;

        bool thunderActive = flags.any(RE::TESWeather::WeatherDataFlag::kRainy) &&
                             w->data.thunderLightningFrequency > 0;

        s_timeSinceFlash += a_deltaTime;
        s_flashCooldown  -= a_deltaTime;

        // Stochastic flash model: higher frequency = more frequent flashes
        // Average interval: ~30s at freq=0.1, ~3s at freq=1.0
        if (thunderActive && s_flashCooldown <= 0.f) {
            float freq = data.Lightning.x;
            float avgInterval = 3.0f / std::max(freq, 0.01f);

            // Simple exponential distribution: P(flash this frame) = dt / avgInterval
            // Using a deterministic accumulator to avoid needing <random>
            static float s_accumulator = 0.f;
            s_accumulator += a_deltaTime / avgInterval;

            if (s_accumulator >= 1.0f) {
                s_accumulator -= 1.0f;
                s_flashIntensity = 0.8f + (freq * 0.2f);  // Stronger with higher frequency
                s_timeSinceFlash = 0.f;
                s_flashCooldown = 0.5f;  // Minimum 0.5s between flashes
            }
        }

        // Flash intensity decay (fast exponential falloff)
        s_flashIntensity *= std::exp(-a_deltaTime * 8.0f);

        data.Lightning.y = (s_flashIntensity > 0.01f) ? 1.f : 0.f;
        data.Lightning.z = s_flashIntensity;
        data.Lightning.w = s_timeSinceFlash;

        // Weather flags
        data.Flags.x = flags.any(RE::TESWeather::WeatherDataFlag::kPleasant) ? 1.f : 0.f;
        data.Flags.y = flags.any(RE::TESWeather::WeatherDataFlag::kCloudy)   ? 1.f : 0.f;
        data.Flags.z = isRain ? 1.f : 0.f;
        data.Flags.w = isSnow ? 1.f : 0.f;

        // Transition
        data.Transition.x = sky->currentWeatherPct;

        if (sky->lastWeather)
            data.Transition.y = static_cast<float>(
                sky->lastWeather->GetFormID() & 0xFFFF);
        data.Transition.z = static_cast<float>(
            w->GetFormID() & 0xFFFF);

        // Surface wetness with drying lag
        // Rain builds wetness quickly, drying is slow (realistic puddle evaporation)
        static float s_surfaceWetness = 0.f;
        static float s_puddleDepth    = 0.f;
        static float s_snowAccum      = 0.f;

        bool raining = (data.Precipitation.x >= 1.0f && data.Precipitation.x < 2.0f);
        bool snowing = (data.Precipitation.x >= 2.0f);
        float precipIntensity = data.Precipitation.y;

        if (raining) {
            // Wetness builds at rate proportional to rain intensity
            s_surfaceWetness += precipIntensity * a_deltaTime * 0.5f;
            s_puddleDepth   += precipIntensity * a_deltaTime * 0.1f;
        } else {
            // Drying: slow exponential decay (~2 minutes to dry from full)
            s_surfaceWetness *= std::exp(-a_deltaTime * 0.008f);
            s_puddleDepth   *= std::exp(-a_deltaTime * 0.004f);
        }

        if (snowing) {
            s_snowAccum += precipIntensity * a_deltaTime * 0.3f;
        } else {
            // Snow melts slowly
            s_snowAccum *= std::exp(-a_deltaTime * 0.002f);
        }

        s_surfaceWetness = std::clamp(s_surfaceWetness, 0.f, 1.f);
        s_puddleDepth    = std::clamp(s_puddleDepth,    0.f, 1.f);
        s_snowAccum      = std::clamp(s_snowAccum,       0.f, 1.f);

        data.PrecipSurface.x = s_surfaceWetness;
        data.PrecipSurface.y = s_puddleDepth;
        data.PrecipSurface.z = s_snowAccum;

        return data;
    }
}

//=============================================================================
// PlayerTracker
//=============================================================================
namespace SB::PlayerTracker
{
    static RE::NiPoint3 s_prevPos{};
    static bool         s_hasPrevPos = false;

    PlayerData Update(float a_deltaTime)
    {
        PlayerData data{};

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player)
            return data;

        RE::Actor* actor = static_cast<RE::Actor*>(player);

        // Position
        auto pos = player->GetPosition();
        data.Position.x = pos.x;
        data.Position.y = pos.y;
        data.Position.z = pos.z;

        float waterZ = 0.f;
        if (auto* cell = player->GetParentCell()) {
            waterZ = cell->GetExteriorWaterHeight();
        }
        data.Position.w = pos.z - waterZ;

        // Vitals
        using AV = RE::ActorValue;

        auto* avOwner = actor->AsActorValueOwner();
        if (avOwner) {
            float maxHP = avOwner->GetPermanentActorValue(AV::kHealth);
            float maxSP = avOwner->GetPermanentActorValue(AV::kStamina);
            float maxMP = avOwner->GetPermanentActorValue(AV::kMagicka);

            data.Vitals.x = (maxHP > 0.f) ? avOwner->GetActorValue(AV::kHealth)  / maxHP : 0.f;
            data.Vitals.y = (maxSP > 0.f) ? avOwner->GetActorValue(AV::kStamina) / maxSP : 0.f;
            data.Vitals.z = (maxMP > 0.f) ? avOwner->GetActorValue(AV::kMagicka) / maxMP : 0.f;
        }
        data.Vitals.w = static_cast<float>(player->GetLevel());

        // Movement — speed in units/second
        if (s_hasPrevPos && a_deltaTime > 0.0001f) {
            float dx = pos.x - s_prevPos.x;
            float dy = pos.y - s_prevPos.y;
            float dz = pos.z - s_prevPos.z;
            data.Movement.x = std::sqrt(dx*dx + dy*dy + dz*dz) / a_deltaTime;
        }
        s_prevPos = pos;
        s_hasPrevPos = true;

        auto* actorState = actor->AsActorState();
        if (actorState) {
            data.Movement.y = actorState->IsSprinting() ? 1.f : 0.f;
            data.Movement.z = actorState->IsSwimming()  ? 1.f : 0.f;
        }

        bool isRiding = player->IsOnMount();
        data.Movement.w = isRiding ? 1.f : 0.f;

        // Combat
        data.Combat.x = actor->IsInCombat()  ? 1.f : 0.f;
        if (actorState) {
            data.Combat.y = actorState->IsBleedingOut() ? 1.f : 0.f;
            data.Combat.w = actorState->IsWeaponDrawn() ? 1.f : 0.f;
        }
        auto* cam = RE::PlayerCamera::GetSingleton();
        if (cam) {
            bool isKillCam = (cam->currentState ==
                cam->cameraStates[RE::CameraState::kVATS]);
            data.Combat.z = isKillCam ? 1.f : 0.f;
        }

        // Water
        float headZ = pos.z + 128.0f;
        bool underwater = (headZ < waterZ && waterZ > -1e10f);

        data.Water.x = underwater ? 1.f : 0.f;
        data.Water.y = waterZ;
        data.Water.z = underwater ? (waterZ - headZ) : 0.f;

        bool wading = (!underwater && pos.z < waterZ && waterZ > -1e10f);
        data.Water.w = wading ? 1.f : 0.f;

        return data;
    }
}

//=============================================================================
// CameraTracker
//=============================================================================
namespace SB::CameraTracker
{
    static Float4x4 s_prevViewProj{};
    static bool     s_hasPrevVP = false;

    static void CopyMatrix(Float4x4& a_dst, const float a_src[4][4])
    {
        for (int r = 0; r < 4; ++r) {
            a_dst.row[r].x = a_src[r][0];
            a_dst.row[r].y = a_src[r][1];
            a_dst.row[r].z = a_src[r][2];
            a_dst.row[r].w = a_src[r][3];
        }
    }

    static void MultiplyMatrix(Float4x4& a_out, const Float4x4& a_a, const Float4x4& a_b)
    {
        for (int r = 0; r < 4; ++r) {
            const float* ar = &a_a.row[r].x;
            for (int c = 0; c < 4; ++c) {
                float sum = 0.f;
                for (int k = 0; k < 4; ++k) {
                    const float* bk = &a_b.row[k].x;
                    sum += ar[k] * bk[c];
                }
                (&a_out.row[r].x)[c] = sum;
            }
        }
    }

    static bool InvertMatrix(Float4x4& a_out, const Float4x4& a_in)
    {
        float m[16], inv[16];
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c)
                m[r*4+c] = (&a_in.row[r].x)[c];

        std::memset(inv, 0, sizeof(inv));
        for (int i = 0; i < 4; ++i) inv[i*5] = 1.f;

        for (int col = 0; col < 4; ++col) {
            int best = col;
            float bestVal = std::abs(m[col*4+col]);
            for (int row = col+1; row < 4; ++row) {
                float v = std::abs(m[row*4+col]);
                if (v > bestVal) { best = row; bestVal = v; }
            }
            if (bestVal < 1e-12f) return false;

            if (best != col) {
                for (int j = 0; j < 4; ++j) {
                    std::swap(m[col*4+j], m[best*4+j]);
                    std::swap(inv[col*4+j], inv[best*4+j]);
                }
            }

            float pivot = m[col*4+col];
            for (int j = 0; j < 4; ++j) {
                m[col*4+j] /= pivot;
                inv[col*4+j] /= pivot;
            }

            for (int row = 0; row < 4; ++row) {
                if (row == col) continue;
                float factor = m[row*4+col];
                for (int j = 0; j < 4; ++j) {
                    m[row*4+j] -= factor * m[col*4+j];
                    inv[row*4+j] -= factor * inv[col*4+j];
                }
            }
        }

        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c)
                (&a_out.row[r].x)[c] = inv[r*4+c];

        return true;
    }

    CameraData Update()
    {
        CameraData data{};

        auto* pcam = RE::PlayerCamera::GetSingleton();
        if (pcam) {
            data.Angles.y = pcam->yaw;
            data.Angles.z = static_cast<float>(pcam->currentState ?
                pcam->currentState->id : 0);
            data.Info.x = pcam->worldFOV;
        }

        auto* niCam = RE::Main::WorldRootCamera();
        if (niCam) {
            auto& camPos = niCam->world.translate;
            data.WorldPos.x = camPos.x;
            data.WorldPos.y = camPos.y;
            data.WorldPos.z = camPos.z;

            auto& rot = niCam->world.rotate;
            float forwardZ = rot.entry[2][1];
            data.Angles.x = std::asin(std::clamp(forwardZ, -1.0f, 1.0f));
        }

        if (niCam) {
            const auto& rd = niCam->GetRuntimeData();
            const auto& rd2 = niCam->GetRuntimeData2();

            CopyMatrix(data.ViewMatrix, rd.worldToCam);

            const auto& port = rd2.port;
            float portW = port.GetWidth();
            float portH = port.GetHeight();

            float n = rd2.viewFrustum.fNear;
            float f = rd2.viewFrustum.fFar;

            data.Info.y = n;
            data.Info.z = f;
            data.Info.w = (portH > 0.f) ? portW / portH : 1.f;

            std::memset(&data.ProjMatrix, 0, sizeof(Float4x4));
            if (portW > 1e-6f && portH > 1e-6f && std::abs(f - n) > 1e-6f) {
                data.ProjMatrix.row[0].x = 2.f * n / portW;
                data.ProjMatrix.row[1].y = 2.f * n / portH;
                data.ProjMatrix.row[2].x = 0.f;
                data.ProjMatrix.row[2].y = 0.f;
                data.ProjMatrix.row[2].z = f / (f - n);
                data.ProjMatrix.row[2].w = 1.f;
                data.ProjMatrix.row[3].z = -n * f / (f - n);
            }
        }

        MultiplyMatrix(data.ViewProjMatrix, data.ViewMatrix, data.ProjMatrix);

        if (!InvertMatrix(data.InvViewProj, data.ViewProjMatrix)) {
            std::memset(&data.InvViewProj, 0, sizeof(Float4x4));
        }

        if (s_hasPrevVP) {
            data.PrevViewProj = s_prevViewProj;
        } else {
            data.PrevViewProj = data.ViewProjMatrix;
        }

        s_prevViewProj = data.ViewProjMatrix;
        s_hasPrevVP = true;

        return data;
    }
}

//=============================================================================
// InteriorTracker
//=============================================================================
namespace SB::InteriorTracker
{
    InteriorData Update()
    {
        InteriorData data{};

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return data;

        auto* cell = player->GetParentCell();
        if (!cell) return data;

        data.IsInterior.x = cell->IsInteriorCell() ? 1.f : 0.f;

        if (!cell->IsInteriorCell())
            return data;

        auto* ld = cell->GetLighting();
        if (!ld) {
            data.IsInterior.y = 0.f;
            return data;
        }

        data.IsInterior.y = 1.f;

        data.AmbientColor = {
            ld->ambient.red   / 255.0f,
            ld->ambient.green / 255.0f,
            ld->ambient.blue  / 255.0f,
            1.0f
        };

        data.DirectionalColor = {
            ld->directional.red   / 255.0f,
            ld->directional.green / 255.0f,
            ld->directional.blue  / 255.0f,
            ld->directionalFade
        };

        float rotX = static_cast<float>(ld->directionalXY) * (3.14159265f / 180.0f);
        float rotZ = static_cast<float>(ld->directionalZ)  * (3.14159265f / 180.0f);
        data.DirectionalDir.x = std::cos(rotX) * std::sin(rotZ);
        data.DirectionalDir.y = std::sin(rotX) * std::sin(rotZ);
        data.DirectionalDir.z = std::cos(rotZ);

        data.InteriorFogColor = {
            ld->fogColorNear.red   / 255.0f,
            ld->fogColorNear.green / 255.0f,
            ld->fogColorNear.blue  / 255.0f,
            0.f
        };

        data.InteriorFogDist.x = ld->fogNear;
        data.InteriorFogDist.y = ld->fogFar;
        data.InteriorFogDist.z = ld->fogPower;
        data.InteriorFogDist.w = ld->clipDist;

        return data;
    }
}

//=============================================================================
// ShadowTracker
//=============================================================================
namespace SB::ShadowTracker
{
    ShadowData Update()
    {
        ShadowData data{};

        auto& shaderState = RE::BSShaderManager::State::GetSingleton();
        (void)shaderState;

        auto* sky = RE::Sky::GetSingleton();
        if (!sky || !sky->sun)
            return data;

        auto* sunRoot = sky->sun->root.get();
        if (!sunRoot)
            return data;

        auto& sunPos = sunRoot->world.translate;
        float dist = std::sqrt(sunPos.x * sunPos.x +
                               sunPos.y * sunPos.y +
                               sunPos.z * sunPos.z);
        if (dist > 0.001f) {
            data.LightDirection.x = sunPos.x / dist;
            data.LightDirection.y = sunPos.y / dist;
            data.LightDirection.z = sunPos.z / dist;
            data.LightDirection.w = 1.0f;
        }

        if (sky->currentWeather) {
            float hour = 12.0f;
            if (auto* cal = RE::Calendar::GetSingleton())
                hour = cal->GetHour();

            // Read sunlight color for diffuse
            auto readColor = [&](int colorType) -> Float4 {
                auto* w = sky->currentWeather;
                auto getSlot = [&](int idx) -> Float4 {
                    idx &= 3;
                    const auto& c = w->colorData[colorType][idx];
                    return { c.red / 255.0f, c.green / 255.0f, c.blue / 255.0f, 0.f };
                };

                int slotA, slotB;
                float t;
                if      (hour < 3.f)  { slotA = 3; slotB = 3; t = 0.f; }
                else if (hour < 9.f)  { slotA = 3; slotB = 0; t = (hour - 3.f) / 6.f; }
                else if (hour < 12.f) { slotA = 0; slotB = 1; t = (hour - 9.f) / 3.f; }
                else if (hour < 15.f) { slotA = 1; slotB = 1; t = 0.f; }
                else if (hour < 18.f) { slotA = 1; slotB = 2; t = (hour - 15.f) / 3.f; }
                else if (hour < 21.f) { slotA = 2; slotB = 3; t = (hour - 18.f) / 3.f; }
                else                  { slotA = 3; slotB = 3; t = 0.f; }

                auto cA = getSlot(slotA);
                auto cB = getSlot(slotB);
                return {
                    cA.x + (cB.x - cA.x) * t,
                    cA.y + (cB.y - cA.y) * t,
                    cA.z + (cB.z - cA.z) * t,
                    0.f
                };
            };

            using CT = RE::TESWeather::ColorTypes;
            data.LightDiffuse = readColor(CT::kSunlight);
            data.LightAmbient = readColor(CT::kAmbient);
        }

        return data;
    }
}

//=============================================================================
// EffectsTracker
//=============================================================================
namespace SB::EffectsTracker
{
    EffectsData Update()
    {
        EffectsData data{};

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player)
            return data;

        auto* magicTarget = player->AsMagicTarget();
        if (!magicTarget)
            return data;

        auto* activeEffects = magicTarget->GetActiveEffectList();
        if (!activeEffects)
            return data;

        for (auto* ae : *activeEffects) {
            if (!ae || ae->flags.any(RE::ActiveEffect::Flag::kInactive))
                continue;

            auto* effect = ae->GetBaseObject();
            if (!effect)
                continue;

            auto archetype = effect->GetArchetype();
            using AT = RE::EffectSetting::Archetype;

            // Vision effects
            if (archetype == AT::kNightEye) {
                data.VisionEffects.x = 1.f;
            }
            if (archetype == AT::kDetectLife) {
                // Detect Life and Detect Dead use the same archetype
                // but Detect Dead targets dead actors. Check the associated
                // actor value to distinguish.
                auto av = effect->data.primaryAV;
                if (av == RE::ActorValue::kNone) {
                    // Detect Dead has no primary AV or uses a unique one
                    data.VisionEffects.z = 1.f;
                } else {
                    data.VisionEffects.y = 1.f;
                }
            }
            if (archetype == AT::kEtherealize) {
                data.VisionEffects.w = 1.f;
            }

            // Time effects
            if (archetype == AT::kSlowTime) {
                data.TimeEffects.x = ae->magnitude;
            }

            // Damage effects
            if (archetype == AT::kValueModifier) {
                auto av = effect->data.primaryAV;
                if (av == RE::ActorValue::kHealth) {
                    auto resist = effect->data.resistVariable;
                    if (resist == RE::ActorValue::kResistFire)
                        data.DamageEffects.x = 1.f;
                    else if (resist == RE::ActorValue::kResistFrost)
                        data.DamageEffects.y = 1.f;
                    else if (resist == RE::ActorValue::kResistShock)
                        data.DamageEffects.z = 1.f;
                    else if (resist == RE::ActorValue::kPoisonResist)
                        data.DamageEffects.w = 1.f;
                }
            }

            // Misc effects
            if (archetype == AT::kInvisibility) {
                data.MiscEffects.x = 1.f;
            }
            if (archetype == AT::kParalysis) {
                data.MiscEffects.y = 1.f;
            }
        }

        return data;
    }
}

//=============================================================================
// RenderTracker
//=============================================================================
namespace SB::RenderTracker
{
    static uint32_t s_frameCount = 0;

    RenderData Update(float a_deltaTime)
    {
        RenderData data{};
        s_frameCount++;

        // Use modular frame count to preserve float32 integer precision
        // float32 loses integer precision at 2^24 = 16,777,216 (~77 hrs at 60fps)
        // Wrap at 2^20 = 1,048,576 (~4.8 hrs) for comfortable margin
        data.FrameInfo.x = static_cast<float>(s_frameCount & 0xFFFFF);
        data.FrameInfo.y = a_deltaTime;

        auto* gfx = RE::BSGraphics::State::GetSingleton();
        if (gfx) {
            data.FrameInfo.z = static_cast<float>(gfx->screenWidth);
            data.FrameInfo.w = static_cast<float>(gfx->screenHeight);
        }

        data.Jitter.z = static_cast<float>(s_frameCount % 16);

        return data;
    }
}
