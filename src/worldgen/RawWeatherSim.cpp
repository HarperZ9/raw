// RawWeatherSim.cpp: First-Principles Orbital Seasons, Surface Thermal Energy Balance,
// Snowpack/Ice Physics, Dispersion, Botanical Phenology & Mesoscale Weather Simulation Engine.
// Original work, MIT.

#include "RawWeatherSim.h"

#include <algorithm>
#include <cmath>

namespace raw::worldgen {
namespace {

constexpr float kPi               = 3.14159265358979323846f;
constexpr float kDegToRad         = kPi / 180.0f;
constexpr float kDryLapseCPerM    = 0.0098f;   // 9.8 C/km dry adiabatic lapse rate
constexpr float kMoistLapseCPerM  = 0.0058f;   // 5.8 C/km moist adiabatic lapse rate
constexpr float kScaleHeightM     = 8434.5f;   // Atmospheric pressure scale height (m)
constexpr float kSolarConstantWm2 = 1361.0f;   // Top-of-atmosphere solar constant (W/m^2)
constexpr float kStefanBoltzmann  = 5.670374e-8f;
constexpr float kLatentFusionJkg  = 334000.0f; // Water/ice latent heat of fusion (J/kg)
constexpr float kSurfaceHeatCapJ  = 38000.0f;  // Effective upper soil/snow skin heat capacity (J/(m^2*K))

float SanitizeFinite(float v, float fallback) {
    return std::isfinite(v) ? v : fallback;
}

float WrapMod(float v, float period, float fallback) {
    if (!std::isfinite(v) || period <= 0.0f) { return fallback; }
    float r = std::fmod(v, period);
    if (r < 0.0f) { r += period; }
    return r;
}

} // namespace

RawWeatherSim::RawWeatherSim() {
    Reset(WeatherSimConfig{});
}

RawWeatherSim::RawWeatherSim(const WeatherSimConfig& cfg) {
    Reset(cfg);
}

const char* RawWeatherSim::GetRegimeName(WeatherRegime regime) {
    switch (regime) {
        case WeatherRegime::ClearAlpine:         return "ClearAlpine";
        case WeatherRegime::DawnValleyMist:      return "DawnValleyMist";
        case WeatherRegime::MountainFogCloudCap: return "MountainFogCloudCap";
        case WeatherRegime::Overcast:            return "Overcast";
        case WeatherRegime::CoastalSeaFog:       return "CoastalSeaFog";
        case WeatherRegime::Rainstorm:           return "Rainstorm";
        case WeatherRegime::Thunderstorm:        return "Thunderstorm";
        case WeatherRegime::Blizzard:            return "Blizzard";
        case WeatherRegime::ArcticClearAurora:   return "ArcticClearAurora";
        default:                                 return "Unknown";
    }
}

const char* RawWeatherSim::GetPrecipPhaseName(PrecipPhase phase) {
    switch (phase) {
        case PrecipPhase::None:              return "None";
        case PrecipPhase::Rain:              return "Rain";
        case PrecipPhase::Sleet:             return "Sleet";
        case PrecipPhase::WetSnow:           return "WetSnow";
        case PrecipPhase::DryPowderBlizzard: return "DryPowderBlizzard";
        default:                             return "Unknown";
    }
}

const char* RawWeatherSim::GetSeasonName(SeasonId season) {
    switch (season) {
        case SeasonId::Spring: return "Spring";
        case SeasonId::Summer: return "Summer";
        case SeasonId::Autumn: return "Autumn";
        case SeasonId::Winter: return "Winter";
        default:               return "Unknown";
    }
}

float RawWeatherSim::ComputeDewPointC(float tempC, float relativeHumidity01) {
    const float t  = Clamp(SanitizeFinite(tempC, 5.0f), -60.0f, 55.0f);
    const float rh = Clamp(SanitizeFinite(relativeHumidity01, 0.5f), 0.02f, 1.0f);
    constexpr float a = 17.625f;
    constexpr float b = 243.04f;
    const float gamma = std::log(rh) + (a * t) / (b + t);
    const float td = (b * gamma) / (a - gamma);
    return std::min(td, t);
}

float RawWeatherSim::ComputeLclMeters(float tempC, float dewPointC) {
    const float spread = std::max(SanitizeFinite(tempC, 5.0f) - SanitizeFinite(dewPointC, 5.0f), 0.0f);
    return 125.0f * spread;
}

float RawWeatherSim::ComputeSolarDeclinationRad(float dayOfYear, float axialTiltDeg) {
    const float doy = WrapMod(dayOfYear, 365.0f, 172.0f);
    const float tiltRad = Clamp(SanitizeFinite(axialTiltDeg, 23.44f), 0.0f, 45.0f) * kDegToRad;
    // Vernal equinox at day ~80, summer solstice ~172 (+tilt), winter solstice ~355 (-tilt):
    return tiltRad * std::sin(2.0f * kPi * (doy - 80.0f) / 365.0f);
}

float RawWeatherSim::ComputeDayLengthHours(float latitudeDeg, float declinationRad) {
    const float latRad = Clamp(SanitizeFinite(latitudeDeg, 60.0f), -89.0f, 89.0f) * kDegToRad;
    const float dec = Clamp(SanitizeFinite(declinationRad, 0.0f), -0.8f, 0.8f);
    const float cosH0 = -std::tan(latRad) * std::tan(dec);
    if (cosH0 <= -1.0f) { return 24.0f; } // Midnight sun
    if (cosH0 >=  1.0f) { return 0.0f;  } // Polar night
    return (24.0f / kPi) * std::acos(cosH0);
}

float RawWeatherSim::ComputeSunElevation(float gameHour0to24) {
    // Equinox-centered Nordic solar elevation convenience wrapper:
    return ComputeOrbitalSunElevation(gameHour0to24, 58.0f, 0.16f);
}

float RawWeatherSim::ComputeOrbitalSunElevation(float gameHour0to24, float latitudeDeg, float declinationRad) {
    const float h = WrapMod(gameHour0to24, 24.0f, 12.0f);
    const float latRad = Clamp(SanitizeFinite(latitudeDeg, 60.0f), -89.0f, 89.0f) * kDegToRad;
    const float dec = Clamp(SanitizeFinite(declinationRad, 0.0f), -0.8f, 0.8f);
    const float hourAngle = ((h - 12.5f) / 24.0f) * (2.0f * kPi);
    const float sinAlt = std::sin(latRad) * std::sin(dec) + std::cos(latRad) * std::cos(dec) * std::cos(hourAngle);
    return Clamp(sinAlt, -1.0f, 1.0f);
}

float RawWeatherSim::WindMpsToBeaufort(float windMps) {
    const float v = std::max(SanitizeFinite(windMps, 0.0f), 0.0f);
    if (v <= 0.05f) { return 0.0f; }
    return Clamp(std::pow(v / 0.836f, 2.0f / 3.0f), 0.0f, 12.0f);
}

RawWeatherSim::RegimeTarget RawWeatherSim::GetRegimeTarget(WeatherRegime regime) {
    switch (regime) {
        case WeatherRegime::ClearAlpine:
            return {1024.0f, +2.0f, 0.42f, 0.14f,  2.8f, 0.45f, 0.00f, 0.00f, 0.05f, 0.10f, 0.15f};
        case WeatherRegime::DawnValleyMist:
            return {1018.0f, -1.5f, 0.86f, 0.28f,  1.4f, 0.70f, 0.00f, 0.00f, 0.25f, 0.95f, 0.05f};
        case WeatherRegime::MountainFogCloudCap:
            return {1009.0f, -2.5f, 0.91f, 0.78f,  6.2f, 1.05f, 0.10f, 0.05f, 0.30f, 0.45f, 0.00f};
        case WeatherRegime::Overcast:
            return {1006.0f, -1.0f, 0.78f, 0.88f,  5.5f, 1.30f, 0.05f, 0.05f, 0.22f, 0.28f, 0.00f};
        case WeatherRegime::CoastalSeaFog:
            return {1012.0f, -3.0f, 0.96f, 0.64f,  3.6f, 2.15f, 0.04f, 0.00f, 0.98f, 0.65f, 0.00f};
        case WeatherRegime::Rainstorm:
            return { 992.0f, -0.5f, 0.93f, 0.94f, 12.5f, 1.65f, 0.82f, 0.22f, 0.35f, 0.50f, 0.00f};
        case WeatherRegime::Thunderstorm:
            return { 978.0f, +1.0f, 0.96f, 0.98f, 18.8f, 1.90f, 0.96f, 1.00f, 0.40f, 0.55f, 0.00f};
        case WeatherRegime::Blizzard:
            return { 974.0f, -14.5f, 0.90f, 0.96f, 23.5f, 2.60f, 0.95f, 0.30f, 0.60f, 0.35f, 0.00f};
        case WeatherRegime::ArcticClearAurora:
            return {1031.0f, -12.0f, 0.48f, 0.08f,  2.2f, 2.95f, 0.00f, 0.00f, 0.12f, 0.14f, 1.00f};
        default:
            return {1013.25f, 0.0f, 0.55f, 0.25f,  3.0f, 0.50f, 0.00f, 0.00f, 0.10f, 0.15f, 0.00f};
    }
}

float RawWeatherSim::SeasonalSeaTempOffsetC() const {
    // Thermal-lagged seasonal cycle (warmest late July ~day 202, coldest late January ~day 20):
    // Scaled gently (+-6.5 C) around annual mean so summer is lush temperate and winter drops well below freezing!
    const float phase = 2.0f * kPi * (cal_.dayOfYear - 110.0f) / 365.0f;
    return 6.5f * std::sin(phase);
}

void RawWeatherSim::UpdateCalendar() {
    cal_.dayOfYear           = WrapMod(cal_.gameDaysPassed, 365.0f, 172.0f);
    cal_.timeOfDayHours      = WrapMod(cfg_.timeOfDayHours, 24.0f, 12.0f);
    cal_.timeScale           = Clamp(SanitizeFinite(cfg_.timeScale, 20.0f), 0.0f, 2000.0f);
    cal_.latitudeDeg         = Clamp(SanitizeFinite(cfg_.latitudeDeg, 60.0f), -89.0f, 89.0f);
    cal_.axialTiltDeg        = Clamp(SanitizeFinite(cfg_.axialTiltDeg, 23.44f), 0.0f, 45.0f);
    cal_.solarDeclinationRad = ComputeSolarDeclinationRad(cal_.dayOfYear, cal_.axialTiltDeg);
    cal_.dayLengthHours      = ComputeDayLengthHours(cal_.latitudeDeg, cal_.solarDeclinationRad);
    cal_.solarElevation      = ComputeOrbitalSunElevation(cal_.timeOfDayHours, cal_.latitudeDeg, cal_.solarDeclinationRad);
    cal_.seasonPhase01       = cal_.dayOfYear / 365.0f;

    if (cal_.dayOfYear >= 79.0f && cal_.dayOfYear < 171.0f) {
        cal_.season = SeasonId::Spring;
    } else if (cal_.dayOfYear >= 171.0f && cal_.dayOfYear < 265.0f) {
        cal_.season = SeasonId::Summer;
    } else if (cal_.dayOfYear >= 265.0f && cal_.dayOfYear < 355.0f) {
        cal_.season = SeasonId::Autumn;
    } else {
        cal_.season = SeasonId::Winter;
    }

    // Spherical sun direction vector (+X East, +Y North, +Z Up):
    const float hourAngle = ((cal_.timeOfDayHours - 12.5f) / 24.0f) * (2.0f * kPi);
    const float cosAlt = std::sqrt(std::max(1.0f - cal_.solarElevation * cal_.solarElevation, 0.0f));
    const float sunX = -std::sin(hourAngle) * cosAlt;
    // Northern hemisphere sun arcs across the southern sky (-Y) around solar noon:
    const float southBias = (cal_.latitudeDeg >= 0.0f) ? -1.0f : 1.0f;
    const float sunY = southBias * std::cos(hourAngle) * cosAlt;
    cal_.sunDirUnit      = Normalize(Vec3{sunX, sunY, cal_.solarElevation});
    cal_.solarAzimuthRad = std::atan2(cal_.sunDirUnit.y, cal_.sunDirUnit.x);

    // Kasten-Young airmass + cloud optical transmittance -> surface solar irradiance Q_sun (W/m^2):
    if (cal_.solarElevation > 0.01f) {
        const float sinE = cal_.solarElevation;
        const float airmass = 1.0f / (sinE + 0.15f * std::pow(std::asin(sinE) * (180.0f / kPi) + 3.885f, -1.253f));
        const float clearTrans = std::pow(0.74f, std::min(airmass, 12.0f));
        const float cloudTrans = 1.0f - 0.72f * cur_.cloudCover01 * cur_.cloudCover01;
        cal_.insolationWm2 = kSolarConstantWm2 * sinE * clearTrans * cloudTrans;
    } else {
        cal_.insolationWm2 = 0.0f;
    }
}

void RawWeatherSim::SeedPhenologyForDay(float dayOfYear) {
    const float doy = WrapMod(dayOfYear, 365.0f, 172.0f);
    PhenologyState& ph = thermal_.phenology;
    ph.photoperiodHours = cal_.dayLengthHours;

    // Seed physically consistent seasonal GDD / CDD & pigment baselines for any starting calendar day:
    if (doy >= 70.0f && doy < 165.0f) {
        // Spring bud break & green-up
        const float t = Smoothstep(75.0f, 145.0f, doy);
        ph.growingDegreeDays   = t * 420.0f;
        ph.chillingDegreeDays  = (1.0f - t) * 320.0f;
        ph.budBreak01          = Smoothstep(78.0f, 125.0f, doy) * (1.0f - Smoothstep(145.0f, 170.0f, doy) * 0.35f);
        ph.chlorophyll01       = Smoothstep(85.0f, 145.0f, doy);
        ph.autumnCarotenoid01  = 0.0f;
        ph.autumnAnthocyanin01 = 0.0f;
        ph.leafAbscission01    = 1.0f - Smoothstep(80.0f, 125.0f, doy);
    } else if (doy >= 165.0f && doy < 252.0f) {
        // Full summer canopy
        ph.growingDegreeDays   = 650.0f;
        ph.chillingDegreeDays  = 0.0f;
        ph.budBreak01          = 0.0f;
        ph.chlorophyll01       = 1.0f;
        ph.autumnCarotenoid01  = 0.0f;
        ph.autumnAnthocyanin01 = 0.0f;
        ph.leafAbscission01    = 0.0f;
    } else if (doy >= 252.0f && doy < 330.0f) {
        // Autumn gold -> crimson -> leaf drop
        const float t = Smoothstep(255.0f, 322.0f, doy);
        ph.growingDegreeDays   = 700.0f;
        ph.chillingDegreeDays  = t * 280.0f;
        ph.budBreak01          = 0.0f;
        ph.chlorophyll01       = 1.0f - Smoothstep(260.0f, 305.0f, doy);
        ph.autumnCarotenoid01  = Smoothstep(256.0f, 286.0f, doy) * (1.0f - Smoothstep(310.0f, 330.0f, doy) * 0.5f);
        ph.autumnAnthocyanin01 = Smoothstep(268.0f, 298.0f, doy) * (1.0f - Smoothstep(312.0f, 330.0f, doy) * 0.5f);
        ph.leafAbscission01    = Smoothstep(282.0f, 325.0f, doy);
    } else {
        // Deep winter dormancy
        ph.growingDegreeDays   = 0.0f;
        ph.chillingDegreeDays  = 450.0f;
        ph.budBreak01          = 0.0f;
        ph.chlorophyll01       = 0.0f;
        ph.autumnCarotenoid01  = 0.0f;
        ph.autumnAnthocyanin01 = 0.0f;
        ph.leafAbscission01    = 1.0f;
    }
}

void RawWeatherSim::Reset(const WeatherSimConfig& cfg) {
    cfg_ = cfg;
    cfg_.unitsPerMeter    = std::max(SanitizeFinite(cfg_.unitsPerMeter, 70.0f), 1.0f);
    cfg_.seaLevelZ        = SanitizeFinite(cfg_.seaLevelZ, -14000.0f);
    cfg_.baseSeaTempC     = Clamp(SanitizeFinite(cfg_.baseSeaTempC, 6.5f), -45.0f, 45.0f);
    cfg_.timeOfDayHours   = WrapMod(cfg_.timeOfDayHours, 24.0f, 7.5f);
    cfg_.initialDayOfYear = WrapMod(cfg_.initialDayOfYear, 365.0f, 172.0f);
    cfg_.timeScale        = Clamp(SanitizeFinite(cfg_.timeScale, 20.0f), 0.0f, 2000.0f);
    cfg_.latitudeDeg      = Clamp(SanitizeFinite(cfg_.latitudeDeg, 60.0f), -89.0f, 89.0f);
    cfg_.axialTiltDeg     = Clamp(SanitizeFinite(cfg_.axialTiltDeg, 23.44f), 0.0f, 45.0f);
    cfg_.regimeBlendSpeed = Clamp(SanitizeFinite(cfg_.regimeBlendSpeed, 0.15f), 0.001f, 20.0f);

    rngState_           = (cfg_.seed != 0u) ? cfg_.seed : 0x9E3779B9u;
    simTimeSec_         = 0.0f;
    frontTimerSec_      = 0.0f;
    lightningCharge01_  = 0.0f;
    lightningFlash01_   = 0.0f;
    lightningCooldownS_ = 0.0f;
    thermalSources_.clear();

    targetRegime_       = cfg_.initialRegime;
    cur_                = GetRegimeTarget(targetRegime_);
    cal_.gameDaysPassed = cfg_.initialDayOfYear + cfg_.timeOfDayHours / 24.0f;
    UpdateCalendar();

    thermal_ = SurfaceThermalState{};
    SeedPhenologyForDay(cal_.dayOfYear);

    const float seaTemp = cfg_.baseSeaTempC + cur_.tempOffsetC + SeasonalSeaTempOffsetC();
    thermal_.surfaceTempC = seaTemp;
    if (cur_.precipRate01 > 0.15f && seaTemp > 0.5f) {
        wetness01_ = Clamp01(0.45f + cur_.precipRate01 * 0.45f);
        puddles01_ = Clamp01(cur_.precipRate01 * 0.60f);
    } else {
        wetness01_ = Clamp01((cur_.rh01 - 0.72f) * 1.4f);
        puddles01_ = 0.0f;
    }
    if (seaTemp <= -1.0f && cur_.precipRate01 > 0.20f) {
        thermal_.snow.depthM      = 0.35f;
        thermal_.snow.albedo      = 0.88f;
        thermal_.snow.densityKgM3 = 125.0f;
        thermal_.phenology.canopySnowLoadKgM2 = 4.5f;
    }
    if (cal_.season == SeasonId::Winter && seaTemp < -2.0f) {
        thermal_.ice.freezingDegreeDays = 25.0f;
        thermal_.ice.iceThicknessM      = 0.12f;
    }

    UpdateGpuUniforms(Vec3{0.0f, 0.0f, cfg_.seaLevelZ + 200.0f * cfg_.unitsPerMeter});
}

void RawWeatherSim::SetRegime(WeatherRegime regime, bool immediate) {
    if (static_cast<uint8_t>(regime) >= static_cast<uint8_t>(WeatherRegime::Count)) {
        regime = WeatherRegime::ClearAlpine;
    }
    targetRegime_ = regime;
    if (immediate) {
        cur_ = GetRegimeTarget(regime);
        const float seaTemp = cfg_.baseSeaTempC + cur_.tempOffsetC + SeasonalSeaTempOffsetC();
        if (cur_.precipRate01 > 0.15f && seaTemp > 0.5f) {
            wetness01_ = Clamp01(0.40f + cur_.precipRate01 * 0.50f);
            puddles01_ = Clamp01(cur_.precipRate01 * 0.65f);
        } else if (cur_.precipRate01 <= 0.05f) {
            wetness01_ = Clamp01((cur_.rh01 - 0.72f) * 1.2f);
            puddles01_ = 0.0f;
        }
        UpdateCalendar();
        UpdateGpuUniforms(Vec3{0.0f, 0.0f, cfg_.seaLevelZ + 200.0f * cfg_.unitsPerMeter});
    }
}

void RawWeatherSim::SetTimeOfDay(float hours0to24) {
    cfg_.timeOfDayHours = WrapMod(hours0to24, 24.0f, 12.0f);
    cal_.gameDaysPassed = std::floor(cal_.gameDaysPassed) + cfg_.timeOfDayHours / 24.0f;
    UpdateCalendar();
    UpdateGpuUniforms(Vec3{0.0f, 0.0f, cfg_.seaLevelZ + 200.0f * cfg_.unitsPerMeter});
}

void RawWeatherSim::SetDayOfYear(float dayOfYear0to365, bool syncPhenology) {
    const float doy = WrapMod(dayOfYear0to365, 365.0f, 172.0f);
    cal_.gameDaysPassed = doy + cfg_.timeOfDayHours / 24.0f;
    UpdateCalendar();
    if (syncPhenology) {
        SeedPhenologyForDay(cal_.dayOfYear);
    }
    UpdateGpuUniforms(Vec3{0.0f, 0.0f, cfg_.seaLevelZ + 200.0f * cfg_.unitsPerMeter});
}

void RawWeatherSim::SetTimeScale(float timeScale) {
    cfg_.timeScale = Clamp(SanitizeFinite(timeScale, 20.0f), 0.0f, 2000.0f);
    cal_.timeScale = cfg_.timeScale;
}

void RawWeatherSim::SetLatitudeDeg(float latitudeDeg) {
    cfg_.latitudeDeg = Clamp(SanitizeFinite(latitudeDeg, 60.0f), -89.0f, 89.0f);
    UpdateCalendar();
}

void RawWeatherSim::SetThermalSources(const ThermalSource* sources, uint32_t count) {
    thermalSources_.clear();
    if (!sources || count == 0u) { return; }
    const uint32_t n = std::min(count, 64u);
    thermalSources_.reserve(n);
    for (uint32_t i = 0u; i < n; ++i) {
        ThermalSource ts = sources[i];
        ts.position.x = SanitizeFinite(ts.position.x, 0.0f);
        ts.position.y = SanitizeFinite(ts.position.y, 0.0f);
        ts.position.z = SanitizeFinite(ts.position.z, cfg_.seaLevelZ);
        ts.radiusU    = Clamp(SanitizeFinite(ts.radiusU, 200.0f), 1.0f, 5000.0f);
        ts.powerWm2   = Clamp(SanitizeFinite(ts.powerWm2, 1200.0f), 0.0f, 25000.0f);
        thermalSources_.push_back(ts);
    }
}

uint32_t RawWeatherSim::NextRng() {
    uint32_t x = rngState_;
    x ^= x << 13u;
    x ^= x >> 17u;
    x ^= x << 5u;
    rngState_ = (x != 0u) ? x : 0xA511E9B3u;
    return rngState_;
}

float RawWeatherSim::NextRng01() {
    return static_cast<float>(NextRng() & 0x00FFFFFFu) * (1.0f / 16777216.0f);
}

float RawWeatherSim::EvaluatePointThermalFluxWm2(Vec3 worldPos) const {
    const float px = SanitizeFinite(worldPos.x, 0.0f);
    const float py = SanitizeFinite(worldPos.y, 0.0f);
    const float pz = SanitizeFinite(worldPos.z, cfg_.seaLevelZ);
    float totalWm2 = 0.0f;
    for (const ThermalSource& src : thermalSources_) {
        const float dx = px - src.position.x;
        const float dy = py - src.position.y;
        const float dz = pz - src.position.z;
        const float d2 = dx * dx + dy * dy + dz * dz;
        const float r2 = src.radiusU * src.radiusU;
        if (d2 < r2) {
            const float u = 1.0f - std::sqrt(d2) / src.radiusU;
            totalWm2 += src.powerWm2 * (u * u);
        }
    }
    return Clamp(totalWm2, 0.0f, 25000.0f);
}

float RawWeatherSim::EvaluateDirectionalSolarWm2(Vec3 worldPos, Vec3 surfaceNormal) const {
    if (cal_.solarElevation <= 0.005f || cal_.insolationWm2 <= 0.0f) { return 0.0f; }
    const Vec3 n = Normalize(Vec3{SanitizeFinite(surfaceNormal.x, 0.0f),
                                  SanitizeFinite(surfaceNormal.y, 0.0f),
                                  SanitizeFinite(surfaceNormal.z, 1.0f)});
    const float wz = SanitizeFinite(worldPos.z, cfg_.seaLevelZ);
    const float altKm = Clamp((wz - cfg_.seaLevelZ) / (cfg_.unitsPerMeter * 1000.0f), 0.0f, 8.0f);
    // High alpine air has slightly less optical scattering -> +6%/km direct insolation:
    const float altBoost = 1.0f + 0.06f * altKm;
    const float directCos = std::max(Dot(n, cal_.sunDirUnit), 0.0f);
    const float directShare = 1.0f - 0.65f * cur_.cloudCover01;
    const float skyView = 0.5f * (1.0f + n.z);
    // Project direct beam onto sloped surface (normalized by flat horizontal sin(elev)) + diffuse sky hemisphere:
    const float directFlux = cal_.insolationWm2 * directShare * (directCos / std::max(cal_.solarElevation, 0.12f));
    const float diffuseFlux = cal_.insolationWm2 * (1.0f - directShare) * skyView;
    return Clamp((directFlux + diffuseFlux) * altBoost, 0.0f, 1400.0f);
}

float RawWeatherSim::EvaluateLocalSnowDepthAt(Vec3 worldPos, Vec3 surfaceNormal) const {
    const Vec3 n = Normalize(Vec3{SanitizeFinite(surfaceNormal.x, 0.0f),
                                  SanitizeFinite(surfaceNormal.y, 0.0f),
                                  SanitizeFinite(surfaceNormal.z, 1.0f)});
    const float slopeHold = Smoothstep(0.32f, 0.78f, n.z);
    const float fireWm2 = EvaluatePointThermalFluxWm2(worldPos);
    // Active fire/hearth/forge carves a physical thawed melt ring:
    const float fireMeltFrac = Smoothstep(90.0f, 650.0f, fireWm2);
    const float solarWm2 = EvaluateDirectionalSolarWm2(worldPos, n);
    const float solarBias = Smoothstep(180.0f, 750.0f, solarWm2) * Smoothstep(-3.0f, 4.0f, thermal_.surfaceTempC) * 0.22f;
    return std::max(thermal_.snow.depthM * slopeHold * (1.0f - fireMeltFrac) - solarBias, 0.0f);
}

void RawWeatherSim::IntegrateSurfaceThermals(float dtRealSec, float dtGameSec, Vec3 camWorldPos, const LocalAtmosphereSample& camAtmo) {
    const float dtPhys = std::max(dtGameSec, dtRealSec);
    if (dtPhys <= 0.0f) { return; }

    const float dtHours = dtPhys / 3600.0f;
    const float dtDays  = dtPhys / 86400.0f;
    const float windSpeedMps = Length(Vec2{camAtmo.localWindMps.x, camAtmo.localWindMps.y});

    // 1) Surface Thermal Energy Balance ODE:
    //    C * dT/dt = Q_solar*(1 - alpha) + Q_LW + Q_fire - Q_rad - Q_conv - L_f * m_dot
    const float surfAlbedo = Lerp(0.18f, thermal_.snow.albedo, Smoothstep(0.01f, 0.10f, thermal_.snow.depthM));
    const float qSolar = cal_.insolationWm2 * (1.0f - surfAlbedo);

    const float tAirK  = Clamp(camAtmo.temperatureC + 273.15f, 180.0f, 340.0f);
    const float tSurfK = Clamp(thermal_.surfaceTempC + 273.15f, 180.0f, 340.0f);
    const float skyEmiss = Clamp(0.70f + 0.18f * cur_.cloudCover01 + 0.08f * camAtmo.relativeHumidity01, 0.65f, 0.98f);
    const float qLW     = skyEmiss * kStefanBoltzmann * std::pow(tAirK, 4.0f);
    const float qRadOut = 0.96f * kStefanBoltzmann * std::pow(tSurfK, 4.0f);
    const float hConv   = 6.5f + 4.2f * windSpeedMps; // W/(m^2*K) turbulent convective transfer coefficient
    const float qConv   = hConv * (thermal_.surfaceTempC - camAtmo.temperatureC);
    const float qFire   = EvaluatePointThermalFluxWm2(camWorldPos);

    float netFluxWm2 = qSolar + qLW + qFire - qRadOut - qConv;

    // 2) Snowpack Accumulation, Compaction, Albedo Aging, Solar/Fire Melt & Wind Dispersion:
    const bool isSolidPrecip = (camAtmo.precipPhase == PrecipPhase::WetSnow || camAtmo.precipPhase == PrecipPhase::DryPowderBlizzard);
    float snowAccumM = 0.0f;
    if (isSolidPrecip && camAtmo.temperatureC < 0.5f && camAtmo.precipIntensity01 > 0.02f) {
        // Real-time-visible snowfall accumulation coupled to timescale (up to ~0.006 m/real-s in blizzard + game-time integral):
        const float ratePerRealS = camAtmo.precipIntensity01 * 0.006f;
        const float ratePerGameS = camAtmo.precipIntensity01 * (0.045f / 3600.0f); // ~4.5 cm/game-hour
        snowAccumM = std::max(ratePerRealS * dtRealSec, ratePerGameS * dtGameSec);
        thermal_.snow.depthM = Clamp(thermal_.snow.depthM + snowAccumM, 0.0f, 3.5f);
        // Fresh snowfall refreshes powder albedo toward 0.88 and reduces surface bulk density:
        const float freshMix = Clamp01(snowAccumM * 18.0f);
        thermal_.snow.ageHours    = Lerp(thermal_.snow.ageHours, 0.0f, freshMix);
        thermal_.snow.densityKgM3 = Lerp(thermal_.snow.densityKgM3, 115.0f, freshMix);
        thermal_.snow.albedo      = Lerp(thermal_.snow.albedo, 0.88f, freshMix);
        thermal_.snow.liquidWater01 *= (1.0f - freshMix);
    }

    // Time-based metamorphic compaction & albedo aging (0.88 fresh powder -> 0.70 dense firn -> 0.55 wet spring granular):
    if (thermal_.snow.depthM > 0.001f) {
        const float warmAgeBoost = 1.0f + Smoothstep(-4.0f, 4.0f, thermal_.surfaceTempC) * 2.5f;
        thermal_.snow.ageHours += dtHours * warmAgeBoost;
        const float firnT = 1.0f - std::exp(-thermal_.snow.ageHours / 48.0f);
        const float wetGranular = Clamp01(Smoothstep(-0.5f, 3.0f, thermal_.surfaceTempC) * 0.75f + thermal_.snow.liquidWater01 * 0.55f);
        const float targetDensity = Lerp(Lerp(115.0f, 380.0f, firnT), 520.0f, wetGranular);
        const float targetAlbedo  = Lerp(Lerp(0.88f, 0.70f, firnT), 0.55f, wetGranular);
        const float metaAlpha = 1.0f - std::exp(-dtHours * 0.12f * warmAgeBoost);
        thermal_.snow.densityKgM3  = Lerp(thermal_.snow.densityKgM3, targetDensity, metaAlpha);
        thermal_.snow.albedo       = Clamp(Lerp(thermal_.snow.albedo, targetAlbedo, metaAlpha), 0.52f, 0.90f);
        thermal_.snow.compaction01 = Clamp01((thermal_.snow.densityKgM3 - 115.0f) / (520.0f - 115.0f));
    } else {
        thermal_.snow.ageHours     = 0.0f;
        thermal_.snow.densityKgM3  = 115.0f;
        thermal_.snow.albedo       = 0.88f;
        thermal_.snow.compaction01 = 0.0f;
        thermal_.snow.liquidWater01 = 0.0f;
    }

    // Wind-driven snow drift / saltation dispersion (activates above ~6 m/s wind):
    thermal_.snow.driftDispersion01 = Clamp01(Smoothstep(5.5f, 20.0f, windSpeedMps) * Smoothstep(0.01f, 0.12f, thermal_.snow.depthM)
                                              * (1.0f - thermal_.snow.liquidWater01 * 0.8f));

    // Latent heat of snowmelt when surface reaches melting point or receives positive melt energy:
    float qLatentMelt = 0.0f;
    float snowMeltM   = 0.0f;
    const float effectiveWarmC = std::max(thermal_.surfaceTempC, camAtmo.temperatureC);
    if (thermal_.snow.depthM > 0.0f && (netFluxWm2 > 0.0f || effectiveWarmC > 0.5f)) {
        const float energyMeltWm2 = std::max(netFluxWm2, 0.0f) + std::max(effectiveWarmC - 0.5f, 0.0f) * 28.0f;
        const float meltMassKgM2  = (energyMeltWm2 * dtPhys) / kLatentFusionJkg;
        // Also keep responsive real-time degree-day melt for short interactive steps:
        const float degreeMeltM   = std::max(effectiveWarmC - 0.5f, 0.0f) * (0.00055f + 0.00085f * Clamp01(cal_.solarElevation)) * dtRealSec;
        const float energyMeltM   = meltMassKgM2 / std::max(thermal_.snow.densityKgM3, 110.0f);
        snowMeltM = std::min(thermal_.snow.depthM, std::max(energyMeltM, degreeMeltM));
        thermal_.snow.depthM = std::max(thermal_.snow.depthM - snowMeltM, 0.0f);
        qLatentMelt = (snowMeltM * thermal_.snow.densityKgM3 * kLatentFusionJkg) / std::max(dtPhys, 1.0e-3f);
        qLatentMelt = std::min(qLatentMelt, std::max(netFluxWm2, 0.0f));
        thermal_.snow.liquidWater01 = Clamp01(thermal_.snow.liquidWater01 + snowMeltM * 12.0f);
    } else if (thermal_.surfaceTempC < -0.5f) {
        // Refreeze liquid snow water into icy crust at dusk/sub-zero:
        thermal_.snow.liquidWater01 = std::max(thermal_.snow.liquidWater01 - 0.15f * dtHours, 0.0f);
    }
    thermal_.snow.meltRunoffRateMps = (dtPhys > 1.0e-4f) ? (snowMeltM / dtPhys) : 0.0f;
    gpu_.snowAccumRateMps = (dtRealSec > 1.0e-4f) ? ((snowAccumM - snowMeltM) / dtRealSec) : 0.0f;

    // Advance skin temperature via bounded implicit-safe Euler step:
    const float dT = ((netFluxWm2 - qLatentMelt) / kSurfaceHeatCapJ) * std::min(dtPhys, 1800.0f);
    thermal_.surfaceTempC = Clamp(thermal_.surfaceTempC + Clamp(dT, -4.0f, 4.0f), -65.0f, 55.0f);
    // Keep snow-covered surface clamped near 0 C while snow is actively melting:
    if (thermal_.snow.depthM > 0.02f && thermal_.surfaceTempC > 0.5f && qFire < 120.0f) {
        thermal_.surfaceTempC = Lerp(thermal_.surfaceTempC, 0.5f, 0.45f);
    }
    thermal_.qSolarWm2      = qSolar;
    thermal_.qLongwaveWm2   = qLW;
    thermal_.qFireWm2       = qFire;
    thermal_.qRadOutWm2     = qRadOut;
    thermal_.qConvWm2       = qConv;
    thermal_.qLatentMeltWm2 = qLatentMelt;

    // 3) Ice Physics: Freezing-Degree-Day accumulation (Stefan's Law), dusk refreeze black ice, rime, spring fracture:
    if (thermal_.surfaceTempC < 0.0f) {
        const float fddInc = (-thermal_.surfaceTempC) * dtDays;
        thermal_.ice.freezingDegreeDays = std::min(thermal_.ice.freezingDegreeDays + fddInc, 2500.0f);
        thermal_.ice.meltingDegreeDays  = std::max(thermal_.ice.meltingDegreeDays - fddInc * 0.5f, 0.0f);
        // Stefan's Law: h_ice (m) ~= 0.027 * sqrt(FDD)
        const float targetIceM = 0.027f * std::sqrt(std::max(thermal_.ice.freezingDegreeDays, 0.0f));
        thermal_.ice.iceThicknessM = std::max(thermal_.ice.iceThicknessM, std::min(targetIceM, 1.8f));
        thermal_.ice.fracture01    = std::max(thermal_.ice.fracture01 - 0.12f * dtHours, 0.0f);
        // Dusk/overnight refreeze of wet surfaces -> glossy black ice:
        if (wetness01_ > 0.08f) {
            thermal_.ice.blackIce01 = Clamp01(thermal_.ice.blackIce01 + wetness01_ * (-thermal_.surfaceTempC) * 0.14f * dtHours);
        }
        // Radiative hoarfrost / windward rime deposition on sub-zero surfaces with sufficient moisture:
        const float frostProx = Clamp01(1.0f - std::max(thermal_.surfaceTempC - camAtmo.dewPointC, 0.0f) / 11.5f)
                              * Smoothstep(0.0f, -2.5f, thermal_.surfaceTempC)
                              * Smoothstep(0.22f, 0.65f, camAtmo.relativeHumidity01);
        thermal_.ice.frostRime01 = Clamp01(thermal_.ice.frostRime01 + frostProx * (0.16f + 0.04f * windSpeedMps) * dtHours);
    } else {
        const float mddInc = thermal_.surfaceTempC * dtDays;
        thermal_.ice.meltingDegreeDays = std::min(thermal_.ice.meltingDegreeDays + mddInc, 1500.0f);
        const float thawLossM = (0.018f * thermal_.surfaceTempC + 0.00004f * qSolar + 0.00025f * qFire) * dtDays;
        if (thermal_.ice.iceThicknessM > 0.0f) {
            thermal_.ice.iceThicknessM = std::max(thermal_.ice.iceThicknessM - thawLossM, 0.0f);
            thermal_.ice.fracture01    = Clamp01(thermal_.ice.fracture01 + (0.08f * thermal_.surfaceTempC + 0.0004f * qSolar) * dtHours);
        } else {
            thermal_.ice.freezingDegreeDays = std::max(thermal_.ice.freezingDegreeDays - mddInc * 2.0f, 0.0f);
            thermal_.ice.fracture01 = 0.0f;
        }
        thermal_.ice.blackIce01  = std::max(thermal_.ice.blackIce01 - (0.25f * thermal_.surfaceTempC + 0.001f * qSolar) * dtHours, 0.0f);
        thermal_.ice.frostRime01 = std::max(thermal_.ice.frostRime01 - (0.35f * thermal_.surfaceTempC + 0.002f * qSolar) * dtHours, 0.0f);
    }

    // 4) Moisture, Valley Thermal Inversion Burn-Off & Turbulent Eddy Diffusivity (K_h):
    const float sunBurn = Clamp01(cal_.solarElevation * 1.45f) * (1.0f - cur_.cloudCover01 * 0.65f);
    // Eddy diffusivity K_h grows with wind shear and solar convective buoyancy:
    thermal_.dispersion.eddyDiffusivityM2S = Clamp(0.6f + 1.85f * windSpeedMps + 0.012f * cal_.insolationWm2, 0.5f, 65.0f);
    const float nightBuild = Smoothstep(0.08f, -0.06f, cal_.solarElevation) * (1.0f - Smoothstep(4.0f, 14.0f, windSpeedMps) * 0.7f);
    const float targetInv  = Clamp01(std::max(nightBuild * (0.55f + 0.45f * cur_.rh01), cur_.valleyMistBias01 * (1.0f - sunBurn * 0.85f)));
    thermal_.dispersion.inversionStrength01 = Lerp(thermal_.dispersion.inversionStrength01, targetInv, 1.0f - std::exp(-dtHours * (0.45f + sunBurn * 1.2f)));
    thermal_.dispersion.fogDispersionRate   = Clamp(0.12f + sunBurn * 0.85f + thermal_.dispersion.eddyDiffusivityM2S * 0.045f, 0.05f, 5.0f);
    thermal_.dispersion.smokeDispersionRate = Clamp(0.20f + thermal_.dispersion.eddyDiffusivityM2S * 0.095f, 0.10f, 8.0f);
    thermal_.dispersion.puddleEvapRate      = Clamp((0.015f + sunBurn * 0.065f + windSpeedMps * 0.0035f) * (1.0f - cur_.rh01 * 0.55f), 0.002f, 0.35f);

    // Feed snowmelt runoff into soil wetness & puddles while applying evaporation:
    const bool isLiquidPrecip = (camAtmo.precipPhase == PrecipPhase::Rain || camAtmo.precipPhase == PrecipPhase::Sleet);
    const float rainWetGain   = isLiquidPrecip ? (camAtmo.precipIntensity01 * 0.22f) : 0.0f;
    const float runoffWetGain = Clamp(snowMeltM * 14.0f, 0.0f, 0.35f);
    wetness01_ = Clamp01(wetness01_ + (rainWetGain - thermal_.dispersion.puddleEvapRate) * dtRealSec + runoffWetGain);
    const float puddleGain = isLiquidPrecip ? (camAtmo.precipIntensity01 * 0.14f) : 0.0f;
    puddles01_ = Clamp01(puddles01_ + (puddleGain - thermal_.dispersion.puddleEvapRate * 0.85f) * dtRealSec + runoffWetGain * 0.55f);

    // 5) Continuous Botanical Phenology driven by cumulative GDD, CDD, photoperiod & canopy snow load:
    PhenologyState& ph = thermal_.phenology;
    ph.photoperiodHours = cal_.dayLengthHours;
    if (camAtmo.temperatureC > 5.0f) {
        ph.growingDegreeDays = std::min(ph.growingDegreeDays + (camAtmo.temperatureC - 5.0f) * dtDays, 2500.0f);
    }
    if (camAtmo.temperatureC < 7.0f) {
        ph.chillingDegreeDays = std::min(ph.chillingDegreeDays + (7.0f - camAtmo.temperatureC) * dtDays, 2500.0f);
    }
    const bool isAutumnHalf = (cal_.dayOfYear >= 220.0f || cal_.dayOfYear < 35.0f);
    if (!isAutumnHalf) {
        // Spring / Summer progression:
        const float springDrive = Smoothstep(45.0f, 220.0f, ph.growingDegreeDays) * Smoothstep(9.5f, 13.5f, ph.photoperiodHours);
        ph.budBreak01          = Lerp(ph.budBreak01, springDrive * (1.0f - Smoothstep(320.0f, 520.0f, ph.growingDegreeDays)), 1.0f - std::exp(-dtDays * 0.4f));
        ph.chlorophyll01       = Lerp(ph.chlorophyll01, Smoothstep(65.0f, 280.0f, ph.growingDegreeDays), 1.0f - std::exp(-dtDays * 0.35f));
        ph.autumnCarotenoid01  = std::max(ph.autumnCarotenoid01 - 0.25f * dtDays, 0.0f);
        ph.autumnAnthocyanin01 = std::max(ph.autumnAnthocyanin01 - 0.25f * dtDays, 0.0f);
        ph.leafAbscission01    = Lerp(ph.leafAbscission01, 1.0f - ph.chlorophyll01, 1.0f - std::exp(-dtDays * 0.4f));
    } else {
        // Autumn / Winter photoperiodic senescence & abscission:
        const float shortDay  = Smoothstep(13.2f, 9.2f, ph.photoperiodHours);
        const float chillW    = Smoothstep(15.0f, 180.0f, ph.chillingDegreeDays);
        const float senesce   = Clamp01(shortDay * 0.65f + chillW * 0.55f);
        ph.chlorophyll01       = Lerp(ph.chlorophyll01, 1.0f - senesce, 1.0f - std::exp(-dtDays * 0.35f));
        ph.autumnCarotenoid01  = Lerp(ph.autumnCarotenoid01, Smoothstep(0.12f, 0.68f, senesce) * (1.0f - ph.leafAbscission01 * 0.6f), 1.0f - std::exp(-dtDays * 0.35f));
        ph.autumnAnthocyanin01 = Lerp(ph.autumnAnthocyanin01, Smoothstep(0.28f, 0.82f, senesce) * (0.45f + 0.55f * sunBurn), 1.0f - std::exp(-dtDays * 0.35f));
        const float dropDrive  = Smoothstep(0.42f, 0.95f, senesce) + Smoothstep(10.0f, 22.0f, windSpeedMps) * 0.20f * senesce;
        ph.leafAbscission01    = Clamp01(Lerp(ph.leafAbscission01, Clamp01(dropDrive), 1.0f - std::exp(-dtDays * 0.45f)));
        ph.budBreak01          = 0.0f;
    }

    // Canopy snow load (kg/m^2) accumulates from snowfall and sheds under strong wind gusts or melt:
    if (snowAccumM > 0.0f) {
        const float interceptFrac = 0.55f * (1.0f - ph.leafAbscission01 * 0.45f);
        ph.canopySnowLoadKgM2 = Clamp(ph.canopySnowLoadKgM2 + snowAccumM * 125.0f * interceptFrac, 0.0f, 38.0f);
    }
    const float windShed = Smoothstep(7.0f, 20.0f, windSpeedMps) * 0.22f * dtHours;
    const float meltShed = (thermal_.surfaceTempC > 0.5f) ? (thermal_.surfaceTempC * 0.35f * dtHours + snowMeltM * 18.0f) : 0.0f;
    ph.canopySnowLoadKgM2 = std::max(ph.canopySnowLoadKgM2 - (windShed + meltShed), 0.0f);
}

void RawWeatherSim::Step(float dtSec, Vec3 camWorldPos) {
    Step(dtSec, cfg_.timeScale, -1.0f, camWorldPos);
}

void RawWeatherSim::Step(float dtRealSec, float timeScale, float gameDaysPassed, Vec3 camWorldPos) {
    const float dt = Clamp(SanitizeFinite(dtRealSec, 0.0f), 0.0f, 10.0f);
    const float ts = Clamp(SanitizeFinite(timeScale, cfg_.timeScale), 0.0f, 2000.0f);
    cfg_.timeScale = ts;

    camWorldPos.x = SanitizeFinite(camWorldPos.x, 0.0f);
    camWorldPos.y = SanitizeFinite(camWorldPos.y, 0.0f);
    camWorldPos.z = SanitizeFinite(camWorldPos.z, cfg_.seaLevelZ);

    simTimeSec_ += dt;
    const float dtGameSec = (cfg_.dayCycleHoursPerS != 0.0f) ? (cfg_.dayCycleHoursPerS * 3600.0f * dt) : (dt * ts);

    if (std::isfinite(gameDaysPassed) && gameDaysPassed >= 0.0f) {
        cal_.gameDaysPassed = gameDaysPassed;
        cfg_.timeOfDayHours = WrapMod(std::fmod(gameDaysPassed, 1.0f) * 24.0f, 24.0f, cfg_.timeOfDayHours);
    } else if (dtGameSec > 0.0f) {
        cal_.gameDaysPassed += dtGameSec / 86400.0f;
        cfg_.timeOfDayHours  = WrapMod(cfg_.timeOfDayHours + dtGameSec / 3600.0f, 24.0f, cfg_.timeOfDayHours);
    }
    UpdateCalendar();

    // Optional autonomous synoptic barometric front evolution:
    if (cfg_.autoFrontEvolution && dt > 0.0f) {
        frontTimerSec_ += dt;
        if (frontTimerSec_ >= 180.0f) {
            frontTimerSec_ = 0.0f;
            const uint32_t nextIdx = (static_cast<uint32_t>(targetRegime_) + 1u + (NextRng() % 3u))
                                   % static_cast<uint32_t>(WeatherRegime::Count);
            targetRegime_ = static_cast<WeatherRegime>(nextIdx);
        }
    }

    // Smooth exponential barometric & meteorological front transition:
    const RegimeTarget tgt = GetRegimeTarget(targetRegime_);
    const float alpha = (dt > 0.0f) ? (1.0f - std::exp(-cfg_.regimeBlendSpeed * dt)) : 0.0f;
    cur_.pressureHpa      = Lerp(cur_.pressureHpa,      tgt.pressureHpa,      alpha);
    cur_.tempOffsetC      = Lerp(cur_.tempOffsetC,      tgt.tempOffsetC,      alpha);
    cur_.rh01             = Lerp(cur_.rh01,             tgt.rh01,             alpha);
    cur_.cloudCover01     = Lerp(cur_.cloudCover01,     tgt.cloudCover01,     alpha);
    cur_.windMps          = Lerp(cur_.windMps,          tgt.windMps,          alpha);
    cur_.windAzimuthRad   = Lerp(cur_.windAzimuthRad,   tgt.windAzimuthRad,   alpha);
    cur_.precipRate01     = Lerp(cur_.precipRate01,     tgt.precipRate01,     alpha);
    cur_.stormEnergy01    = Lerp(cur_.stormEnergy01,    tgt.stormEnergy01,    alpha);
    cur_.seaFogBias01     = Lerp(cur_.seaFogBias01,     tgt.seaFogBias01,     alpha);
    cur_.valleyMistBias01 = Lerp(cur_.valleyMistBias01, tgt.valleyMistBias01, alpha);
    cur_.auroraBias01     = Lerp(cur_.auroraBias01,     tgt.auroraBias01,     alpha);

    const LocalAtmosphereSample camAtmo = SampleAt(camWorldPos);
    IntegrateSurfaceThermals(dt, dtGameSec, camWorldPos, camAtmo);

    // Thunderstorm electrostatic charge accumulation & stochastic deterministic discharge flashes:
    if (dt > 0.0f) {
        lightningFlash01_ *= std::exp(-7.5f * dt);
        if (lightningFlash01_ < 1.0e-4f) { lightningFlash01_ = 0.0f; }
        lightningCooldownS_ = std::max(lightningCooldownS_ - dt, 0.0f);

        if (cur_.stormEnergy01 > 0.35f) {
            const float chargeRate = (cur_.stormEnergy01 - 0.30f) * (0.35f + 0.45f * cur_.precipRate01);
            lightningCharge01_ = Clamp01(lightningCharge01_ + chargeRate * dt);
            if (lightningCharge01_ >= 0.82f && lightningCooldownS_ <= 0.0f) {
                const float r = NextRng01();
                if (r < (0.35f + 0.55f * cur_.stormEnergy01)) {
                    lightningFlash01_   = Clamp(0.72f + 0.28f * NextRng01(), 0.0f, 1.0f);
                    lightningCharge01_  = 0.12f * NextRng01();
                    lightningCooldownS_ = 1.6f + 3.2f * NextRng01();
                }
            }
        } else {
            lightningCharge01_ = std::max(lightningCharge01_ - 0.25f * dt, 0.0f);
        }
    }

    UpdateGpuUniforms(camWorldPos);
}

LocalAtmosphereSample RawWeatherSim::SampleAt(Vec3 worldPos) const {
    LocalAtmosphereSample s{};
    const float wxIn = SanitizeFinite(worldPos.x, 0.0f);
    const float wyIn = SanitizeFinite(worldPos.y, 0.0f);
    const float wz   = SanitizeFinite(worldPos.z, cfg_.seaLevelZ);
    const float altM = Clamp((wz - cfg_.seaLevelZ) / cfg_.unitsPerMeter, -2000.0f, 15000.0f);
    const float altPosM = std::max(altM, 0.0f);
    s.altitudeMeters = altM;

    // Sea-level synoptic baseline + orbital seasonal cycle + diurnal swing:
    const float diurnalC = 2.5f * std::cos(((cfg_.timeOfDayHours - 14.5f) / 24.0f) * (2.0f * kPi));
    const float seaTempC = cfg_.baseSeaTempC + cur_.tempOffsetC + SeasonalSeaTempOffsetC()
                         + diurnalC * (1.0f - cur_.cloudCover01 * 0.55f);
    const float seaDewC  = ComputeDewPointC(seaTempC, cur_.rh01);
    const float lclM     = ComputeLclMeters(seaTempC, seaDewC);

    // Piecewise dry-then-moist adiabatic temperature profile (strictly decreasing with elevation!):
    float tempC = 0.0f;
    if (altPosM <= lclM) {
        tempC = seaTempC - kDryLapseCPerM * altPosM;
    } else {
        const float tAtLcl = seaTempC - kDryLapseCPerM * lclM;
        tempC = tAtLcl - kMoistLapseCPerM * (altPosM - lclM);
    }
    if (altM < 0.0f) {
        tempC = seaTempC - kDryLapseCPerM * altM;
    }
    tempC = Clamp(tempC, -85.0f, 65.0f);

    // Hypsometric barometric pressure P(z) = P0 * exp(-z / H):
    const float pressureHpa = cur_.pressureHpa * std::exp(-altM / kScaleHeightM);

    // Dew point drops ~2.0 C/km below LCL, converges to T above LCL (condensation saturation):
    float dewC = seaDewC - 0.0020f * altPosM;
    if (altPosM >= lclM) {
        dewC = std::min(tempC, dewC + (tempC - dewC) * Smoothstep(lclM, lclM + 120.0f, altPosM));
    }
    dewC = Clamp(std::min(dewC, tempC), -90.0f, 65.0f);

    constexpr float a = 17.625f;
    constexpr float b = 243.04f;
    const float rhLocal = Clamp01(std::exp((a * dewC) / (b + dewC) - (a * tempC) / (b + tempC)));

    s.pressureHpa        = pressureHpa;
    s.temperatureC       = tempC;
    s.dewPointC          = dewC;
    s.relativeHumidity01 = rhLocal;
    s.lclAltitudeMeters  = lclM;
    s.zLclU              = cfg_.seaLevelZ + lclM * cfg_.unitsPerMeter;

    // Orographic mountain fog / cloud cap peaks around and above LCL:
    const float bandWidthM  = Lerp(220.0f, 520.0f, cur_.cloudCover01);
    const float lclDist     = (altPosM - (lclM + bandWidthM * 0.35f)) / std::max(bandWidthM, 50.0f);
    const float lclEnvelope = std::exp(-lclDist * lclDist * 1.15f) * Smoothstep(lclM - 160.0f, lclM + 60.0f, altPosM);
    s.orographicFog01 = Clamp01(lclEnvelope * Clamp01(0.25f + cur_.cloudCover01 * 0.75f + cur_.precipRate01 * 0.35f));

    // Valley thermal inversion mist peaks near dawn/dusk and dissipates with solar burn-off:
    const float dawnW = std::exp(-std::pow((cfg_.timeOfDayHours - 6.2f) / 2.6f, 2.0f));
    const float duskW = std::exp(-std::pow((cfg_.timeOfDayHours - 19.6f) / 2.5f, 2.0f));
    const float sunBurn = Clamp01(cal_.solarElevation * 1.4f) * (1.0f - cur_.cloudCover01 * 0.65f);
    const float invW  = Clamp01((std::max(dawnW, duskW) * 0.85f + cur_.valleyMistBias01 * 0.75f) * (1.0f - sunBurn * 0.65f));
    const float valleyDecay = std::exp(-std::max(altM, 0.0f) / Lerp(85.0f, 220.0f, invW));
    s.valleyMist01 = Clamp01(valleyDecay * invW * Clamp01(0.35f + 0.65f * rhLocal));

    // Coastal sea-fog hugs low marine elevations (< 160m above sea level):
    const float seaDecay = std::exp(-std::max(altM, 0.0f) / 95.0f);
    s.coastalSeaFog01 = Clamp01(seaDecay * cur_.seaFogBias01 * Clamp01(0.30f + 0.70f * rhLocal));

    // Topographic ridge wind acceleration:
    const float gustWave = 1.0f + 0.18f * std::sin(simTimeSec_ * 0.65f + 0.00018f * (wxIn + wyIn))
                                + 0.09f * std::cos(simTimeSec_ * 1.42f - 0.00031f * wyIn);
    const float ridgeBoost = 1.0f + 0.55f * Smoothstep(150.0f, 1800.0f, altPosM);
    const float localWindSpeed = std::max(cur_.windMps * ridgeBoost * gustWave, 0.0f);
    const float wx = std::cos(cur_.windAzimuthRad) * localWindSpeed;
    const float wy = std::sin(cur_.windAzimuthRad) * localWindSpeed;
    const float wzWind = (cur_.stormEnergy01 > 0.4f) ? (0.8f * cur_.stormEnergy01) : (-0.25f * Smoothstep(400.0f, 1600.0f, altPosM));
    s.localWindMps = Vec3{wx, wy, wzWind};

    // Altitude- and temperature-driven precipitation phase transition:
    const float orographicPrecipBoost = 1.0f + 0.35f * Smoothstep(300.0f, 1800.0f, altPosM);
    const float effPrecip = Clamp01(cur_.precipRate01 * orographicPrecipBoost);
    s.precipIntensity01 = effPrecip;
    if (effPrecip <= 0.03f) {
        s.precipPhase = PrecipPhase::None;
    } else if (tempC > 2.0f) {
        s.precipPhase = PrecipPhase::Rain;
    } else if (tempC > 0.0f) {
        s.precipPhase = PrecipPhase::Sleet;
    } else if (tempC > -4.0f) {
        s.precipPhase = PrecipPhase::WetSnow;
    } else {
        s.precipPhase = PrecipPhase::DryPowderBlizzard;
    }

    return s;
}

void RawWeatherSim::UpdateGpuUniforms(Vec3 camWorldPos) {
    const LocalAtmosphereSample camAtmo = SampleAt(camWorldPos);
    const float sunElev  = cal_.solarElevation;
    const float diurnalC = 2.5f * std::cos(((cfg_.timeOfDayHours - 14.5f) / 24.0f) * (2.0f * kPi));
    const float seaTempC = cfg_.baseSeaTempC + cur_.tempOffsetC + SeasonalSeaTempOffsetC()
                         + diurnalC * (1.0f - cur_.cloudCover01 * 0.55f);
    const float seaDewC  = ComputeDewPointC(seaTempC, cur_.rh01);
    const float lclM     = ComputeLclMeters(seaTempC, seaDewC);

    const bool isSnow = (camAtmo.precipPhase == PrecipPhase::WetSnow || camAtmo.precipPhase == PrecipPhase::DryPowderBlizzard);
    const bool isRain = (camAtmo.precipPhase == PrecipPhase::Rain || camAtmo.precipPhase == PrecipPhase::Sleet);

    const float rainVal = isRain ? camAtmo.precipIntensity01 : 0.0f;
    const float snowVal = isSnow ? camAtmo.precipIntensity01 : Clamp01(thermal_.snow.depthM * 0.45f);

    const float windSpeedMps = Length(Vec2{camAtmo.localWindMps.x, camAtmo.localWindMps.y});
    const float beaufort     = WindMpsToBeaufort(windSpeedMps);
    const Vec2  windUnitsPerS{camAtmo.localWindMps.x * cfg_.unitsPerMeter * 0.35f,
                              camAtmo.localWindMps.y * cfg_.unitsPerMeter * 0.35f};

    const float nightMask = Smoothstep(0.02f, -0.14f, sunElev);
    const float clearMask = 1.0f - Smoothstep(0.15f, 0.65f, cur_.cloudCover01);
    const float aurora    = Clamp01(cur_.auroraBias01 * nightMask * clearMask);

    gpu_.weather         = Vec4{wetness01_, puddles01_, snowVal, rainVal};
    gpu_.timeEnv         = Vec4{cfg_.timeOfDayHours, sunElev, lightningFlash01_, 0.0f};
    gpu_.cloudWind       = Vec4{windUnitsPerS.x, windUnitsPerS.y, cur_.cloudCover01,
                                std::max(camAtmo.valleyMist01, camAtmo.coastalSeaFog01)};
    gpu_.seaLevelAndTemp = Vec2{cfg_.seaLevelZ, seaTempC};
    gpu_.seasonThermal0  = Vec4{thermal_.snow.depthM, thermal_.snow.albedo, thermal_.snow.compaction01, thermal_.surfaceTempC};
    gpu_.seasonThermal1  = Vec4{thermal_.ice.iceThicknessM, thermal_.ice.blackIce01, thermal_.ice.frostRime01, thermal_.ice.fracture01};
    gpu_.phenology0      = Vec4{thermal_.phenology.budBreak01, thermal_.phenology.chlorophyll01,
                                thermal_.phenology.autumnCarotenoid01, thermal_.phenology.autumnAnthocyanin01};
    gpu_.phenology1      = Vec4{thermal_.phenology.leafAbscission01, thermal_.phenology.canopySnowLoadKgM2,
                                thermal_.dispersion.inversionStrength01, thermal_.dispersion.puddleEvapRate};

    gpu_.beaufortScale         = beaufort;
    gpu_.fireSmokeDeflectVec   = Vec3{camAtmo.localWindMps.x * cfg_.unitsPerMeter * 0.18f,
                                      camAtmo.localWindMps.y * cfg_.unitsPerMeter * 0.18f,
                                      camAtmo.localWindMps.z * cfg_.unitsPerMeter * 0.10f};
    gpu_.snowpackDepthM        = thermal_.snow.depthM;
    gpu_.lightningCharge01     = lightningCharge01_;
    gpu_.lightningFlash01      = lightningFlash01_;
    gpu_.zLclU                 = cfg_.seaLevelZ + lclM * cfg_.unitsPerMeter;
    gpu_.valleyInversionDepthU = Lerp(680.0f, 1650.0f, Clamp01(thermal_.dispersion.inversionStrength01 * 0.7f + cur_.valleyMistBias01 * 0.4f));
    gpu_.barometricPressureHpa = cur_.pressureHpa;
    gpu_.auroraIntensity01     = aurora;
    gpu_.relativeHumidity01    = cur_.rh01;
    gpu_.dewPointC             = seaDewC;
}

} // namespace raw::worldgen
