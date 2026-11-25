#pragma once
// RawWeatherSim.h: First-Principles Orbital Seasons, Surface Thermal Energy Balance,
// Snowpack/Ice Accumulation & Dispersion, Botanical Phenology & Mesoscale Weather Engine (`raw::worldgen`).
// Standalone C++23 clean-room MIT meteorology/thermodynamics simulator with zero Skyrim or graphics API headers.
// Simulates:
//   - Orbital seasons (365-day axial tilt solar declination, latitude day length, seasonal insolation, live timeScale)
//   - Surface thermal energy balance ODE (solar absorption, LW sky radiation, fire/forge heat, radiative/convective loss, latent melt)
//   - Snowpack accumulation, time-based metamorphic compaction & albedo aging (0.88 powder -> 0.70 firn -> 0.55 wet granular),
//     directional slope/aspect solar melt, point-fire/forge/magma heat-radius melt rings, melt runoff, wind drift dispersion
//   - Freezing-degree-day lake/river/shoreline ice thickness (Stefan's law), dusk black-ice refreeze, hoarfrost rime, spring fracture
//   - Sunrise solar burn-off breaking nocturnal valley thermal inversions & wind eddy diffusivity (K_h) dispersing fog/smoke/puddles
//   - Continuous botanical phenology (budBreak, chlorophyll, carotenoid gold, anthocyanin crimson, abscission, canopy snow load)
//   - Synoptic barometric fronts, adiabatic lapse rate, Magnus-Tetens dew point, Espy LCL, Beaufort sea state, lightning
// Original work, MIT.

#include "RawTerrainGen.h"

#include <cstdint>
#include <vector>

namespace raw::worldgen {

enum class WeatherRegime : uint8_t {
    ClearAlpine         = 0,
    DawnValleyMist      = 1,
    MountainFogCloudCap = 2,
    Overcast            = 3,
    CoastalSeaFog       = 4,
    Rainstorm           = 5,
    Thunderstorm        = 6,
    Blizzard            = 7,
    ArcticClearAurora   = 8,
    Count               = 9,
};

enum class PrecipPhase : uint8_t {
    None              = 0,
    Rain              = 1,
    Sleet             = 2,
    WetSnow           = 3,
    DryPowderBlizzard = 4,
};

enum class SeasonId : uint8_t {
    Spring = 0,
    Summer = 1,
    Autumn = 2,
    Winter = 3,
};

struct CalendarState {
    float    gameDaysPassed      = 172.0f; // Continuous cumulative game days (0 = Jan 1 winter, ~172 = summer solstice)
    float    dayOfYear           = 172.0f; // [0, 365)
    float    timeOfDayHours      = 7.5f;   // [0, 24)
    float    timeScale           = 20.0f;  // Game-seconds per real-second (Skyrim default = 20)
    float    latitudeDeg         = 60.0f;  // Nordic latitude (degrees North)
    float    axialTiltDeg        = 23.44f; // Planetary obliquity (degrees)
    float    solarDeclinationRad = 0.0f;   // Orbital solar declination delta(dayOfYear)
    float    dayLengthHours      = 12.0f;  // Exact astronomical daylight duration (hours)
    float    solarElevation      = 0.0f;   // sin(altitude) in [-1, +1]
    float    solarAzimuthRad     = 0.0f;   // Compass solar azimuth (radians)
    Vec3     sunDirUnit{0.0f, -0.5f, 0.866f};
    float    insolationWm2       = 0.0f;   // Surface clear/cloudy incoming solar irradiance Q_sun (W/m^2)
    SeasonId season              = SeasonId::Summer;
    float    seasonPhase01       = 0.47f;  // Continuous [0, 1) orbital year progress
};

struct ThermalSource {
    Vec3  position{};
    float radiusU  = 240.0f;  // Melt/heat influence radius (world units)
    float powerWm2 = 1800.0f; // Peak core radiant/convective heat flux (W/m^2)
};

struct SnowpackState {
    float depthM            = 0.0f;   // Integrated snowpack thickness (meters)
    float densityKgM3       = 120.0f; // 110 fresh powder -> 380 dense firn -> 520 wet spring granular
    float ageHours          = 0.0f;   // Metamorphic age (game hours) since last fresh snowfall
    float albedo            = 0.88f;  // 0.88 fresh powder -> 0.70 dense firn -> 0.55 wet spring granular
    float compaction01      = 0.0f;   // Normalized compaction [0..1]
    float liquidWater01     = 0.0f;   // Melt slush fraction [0..1]
    float driftDispersion01 = 0.0f;   // Wind-driven snow saltation/scouring dispersion [0..1]
    float meltRunoffRateMps = 0.0f;   // Liquid meltwater runoff rate feeding soil wetness & puddles
};

struct IceState {
    float freezingDegreeDays = 0.0f; // Cumulative sub-zero degree-days
    float meltingDegreeDays  = 0.0f; // Cumulative above-zero thaw degree-days
    float iceThicknessM      = 0.0f; // Lake/river/shoreline crystalline ice thickness (Stefan's Law)
    float blackIce01         = 0.0f; // Dusk/overnight surface refreeze glaze on wet ground/roads [0..1]
    float frostRime01        = 0.0f; // Hoarfrost / windward rime crystal buildup [0..1]
    float fracture01         = 0.0f; // Spring freeze-thaw fracture/lead dispersion [0..1]
};

struct DispersionState {
    float inversionStrength01 = 0.0f; // Nocturnal valley thermal inversion strength (burns off after sunrise!)
    float eddyDiffusivityM2S  = 1.0f; // Turbulent eddy diffusivity K_h (m^2/s)
    float fogDispersionRate   = 0.0f; // Combined solar burn-off + turbulent fog clearing rate (1/hr)
    float smokeDispersionRate = 0.0f; // Plume/smoke turbulent dispersion rate (1/hr)
    float puddleEvapRate      = 0.0f; // Puddle & surface moisture evaporation rate
};

struct PhenologyState {
    float growingDegreeDays   = 0.0f;  // Cumulative GDD (> 5 C)
    float chillingDegreeDays  = 0.0f;  // Cumulative autumn/winter chill (< 7 C)
    float photoperiodHours    = 12.0f; // Current day length driving photoperiodic senescence
    float budBreak01          = 0.0f;  // Spring bud flush [0..1]
    float chlorophyll01       = 1.0f;  // Active green leaf chlorophyll [0..1]
    float autumnCarotenoid01  = 0.0f;  // Autumn golden-amber carotenoid pigment [0..1]
    float autumnAnthocyanin01 = 0.0f;  // Frost-triggered crimson-scarlet anthocyanin pigment [0..1]
    float leafAbscission01    = 0.0f;  // Deciduous autumn/winter leaf drop [0..1]
    float canopySnowLoadKgM2  = 0.0f;  // Snow mass resting on tree boughs (kg/m^2, drives cantilever droop!)
};

struct SurfaceThermalState {
    float           surfaceTempC   = 6.0f; // Integrated ground/snow surface temperature (Celsius)
    float           qSolarWm2      = 0.0f; // Absorbed solar flux Q_solar * (1 - alpha)
    float           qLongwaveWm2   = 0.0f; // Downwelling sky/cloud longwave flux Q_LW
    float           qFireWm2       = 0.0f; // Local point-fire/forge/geothermal flux Q_fire
    float           qRadOutWm2     = 0.0f; // Outgoing thermal blackbody loss Q_rad
    float           qConvWm2       = 0.0f; // Turbulent sensible heat exchange Q_conv
    float           qLatentMeltWm2 = 0.0f; // Latent heat consumed by snow/ice phase change L_f * m_dot
    SnowpackState   snow{};
    IceState        ice{};
    DispersionState dispersion{};
    PhenologyState  phenology{};
};

struct WeatherSimConfig {
    uint32_t      seed               = 20261125u;
    float         seaLevelZ          = -14000.0f; // Sea of Ghosts default datum (world units)
    float         unitsPerMeter      = 70.0f;     // Creation Engine scale (70u = 1m)
    float         baseSeaTempC       = 6.5f;      // Annual mean sea-level temperature in Celsius
    float         timeOfDayHours     = 7.5f;      // Initial game hour [0, 24)
    float         initialDayOfYear   = 172.0f;    // Initial orbital day [0, 365) (default: early summer)
    float         timeScale          = 20.0f;     // Game-seconds per real-second
    float         latitudeDeg        = 60.0f;     // Nordic latitude (degrees North)
    float         axialTiltDeg       = 23.44f;    // Obliquity (degrees)
    float         dayCycleHoursPerS  = 0.0f;      // Optional legacy override (0 = use timeScale)
    bool          autoFrontEvolution = false;     // Optional autonomous synoptic front cycling
    float         regimeBlendSpeed   = 0.15f;     // Smooth front transition rate (1/s)
    WeatherRegime initialRegime      = WeatherRegime::ClearAlpine;
};

struct LocalAtmosphereSample {
    float       altitudeMeters       = 0.0f;
    float       pressureHpa          = 1013.25f;
    float       temperatureC         = 6.5f;
    float       dewPointC            = 2.0f;
    float       relativeHumidity01   = 0.65f;
    float       lclAltitudeMeters    = 560.0f;
    float       zLclU                = 25200.0f;
    float       orographicFog01      = 0.0f;
    float       valleyMist01         = 0.0f;
    float       coastalSeaFog01      = 0.0f;
    PrecipPhase precipPhase          = PrecipPhase::None;
    float       precipIntensity01    = 0.0f;
    Vec3        localWindMps{3.0f, 1.5f, 0.0f};
};

// Direct GPU uniform pack matching LightingCommon.hlsli, RawWeatherAtmo.hlsli & RawSeasonsThermals.hlsli:
struct WeatherGpuUniforms {
    Vec4  weather{};               // x=wetness, y=puddles, z=snow, w=rain
    Vec4  timeEnv{};               // x=gameHour (0..24), y=sunElev (-1..+1), z=lightningFlash01, w=0
    Vec4  cloudWind{};             // x,y=wind (world units/s), z=cloudCover01, w=mistStrength01
    Vec2  seaLevelAndTemp{};       // x=seaLevelZ (world units), y=seaTempC (Celsius)
    Vec4  seasonThermal0{};        // x=snowDepthM, y=snowAlbedo, z=snowCompaction01, w=surfaceTempC
    Vec4  seasonThermal1{};        // x=iceThicknessM, y=blackIce01, z=frostRime01, w=iceFracture01
    Vec4  phenology0{};            // x=budBreak01, y=chlorophyll01, z=autumnCarotenoid01, w=autumnAnthocyanin01
    Vec4  phenology1{};            // x=leafAbscission01, y=canopySnowLoadKgM2, z=inversionStrength01, w=puddleEvapRate
    float beaufortScale        = 1.0f;      // 0..12 sea state coupling directly to water waves
    Vec3  fireSmokeDeflectVec{};            // World-space wind deflection vector for fire, smoke & embers (u/s)
    float snowAccumRateMps     = 0.0f;      // >0 snowpack buildup (m/s), <0 thermal melt rate (m/s)
    float snowpackDepthM       = 0.0f;      // Running integrated snowpack depth (meters)
    float lightningCharge01    = 0.0f;      // Electrostatic storm cloud charge [0..1]
    float lightningFlash01     = 0.0f;      // Active intra-cloud/ground discharge flash [0..1]
    float zLclU                = 0.0f;      // Lifting Condensation Level (absolute world Z)
    float valleyInversionDepthU = 800.0f;   // Cool-air valley thermal inversion thickness (world units)
    float barometricPressureHpa = 1013.25f; // Sea-level synoptic barometric pressure (965..1035 hPa)
    float auroraIntensity01    = 0.0f;      // High-latitude clear-night aurora intensity [0..1]
    float relativeHumidity01   = 0.55f;     // Sea-level relative humidity [0..1]
    float dewPointC            = 0.0f;      // Sea-level Magnus-Tetens dew point (Celsius)
};

class RawWeatherSim {
public:
    RawWeatherSim();
    explicit RawWeatherSim(const WeatherSimConfig& cfg);

    void Reset(const WeatherSimConfig& cfg = {});
    void SetRegime(WeatherRegime regime, bool immediate = false);
    void SetTimeOfDay(float hours0to24);
    void SetDayOfYear(float dayOfYear0to365, bool syncPhenology = true);
    void SetTimeScale(float timeScale);
    void SetLatitudeDeg(float latitudeDeg);
    void SetThermalSources(const ThermalSource* sources, uint32_t count);

    // Primary orbital + timescale + thermal physics step:
    // If gameDaysPassed >= 0, synchronizes cumulative calendar day directly; if < 0, advances using dtRealSec * timeScale.
    void Step(float dtRealSec, float timeScale, float gameDaysPassed, Vec3 camWorldPos);

    // Convenience overload using configured timeScale:
    void Step(float dtSec, Vec3 camWorldPos);

    // Evaluates local physical atmospheric conditions at ANY 3D world point (exact lapse rate,
    // hypsometric pressure, Espy LCL, altitude-dependent rain/sleet/snow phase, topographic wind).
    LocalAtmosphereSample SampleAt(Vec3 worldPos) const;

    // Spatial thermal queries (directional aspect/slope solar flux, fire heat-radius melt rings, local snow depth):
    float EvaluatePointThermalFluxWm2(Vec3 worldPos) const;
    float EvaluateDirectionalSolarWm2(Vec3 worldPos, Vec3 surfaceNormal) const;
    float EvaluateLocalSnowDepthAt(Vec3 worldPos, Vec3 surfaceNormal) const;

    const WeatherGpuUniforms&  GetGpuUniforms() const { return gpu_; }
    const CalendarState&       GetCalendarState() const { return cal_; }
    const SurfaceThermalState& GetSurfaceThermalState() const { return thermal_; }
    const PhenologyState&      GetPhenologyState() const { return thermal_.phenology; }
    WeatherRegime              GetActiveRegime() const { return targetRegime_; }
    float                      GetSimTimeSec() const { return simTimeSec_; }

    static const char* GetRegimeName(WeatherRegime regime);
    static const char* GetPrecipPhaseName(PrecipPhase phase);
    static const char* GetSeasonName(SeasonId season);

    // Pure first-principles astronomical & thermodynamic helpers:
    static float ComputeDewPointC(float tempC, float relativeHumidity01);
    static float ComputeLclMeters(float tempC, float dewPointC);
    static float ComputeSolarDeclinationRad(float dayOfYear, float axialTiltDeg = 23.44f);
    static float ComputeDayLengthHours(float latitudeDeg, float declinationRad);
    static float ComputeSunElevation(float gameHour0to24);
    static float ComputeOrbitalSunElevation(float gameHour0to24, float latitudeDeg, float declinationRad);
    static float WindMpsToBeaufort(float windMps);

private:
    struct RegimeTarget {
        float pressureHpa;
        float tempOffsetC;
        float rh01;
        float cloudCover01;
        float windMps;
        float windAzimuthRad;
        float precipRate01;
        float stormEnergy01;
        float seaFogBias01;
        float valleyMistBias01;
        float auroraBias01;
    };

    static RegimeTarget GetRegimeTarget(WeatherRegime regime);
    float SeasonalSeaTempOffsetC() const;
    void UpdateCalendar();
    void SeedPhenologyForDay(float dayOfYear);
    void IntegrateSurfaceThermals(float dtRealSec, float dtGameSec, Vec3 camWorldPos, const LocalAtmosphereSample& camAtmo);
    void UpdateGpuUniforms(Vec3 camWorldPos);
    uint32_t NextRng();
    float NextRng01();

    WeatherSimConfig           cfg_{};
    WeatherRegime              targetRegime_ = WeatherRegime::ClearAlpine;
    RegimeTarget               cur_{};
    CalendarState              cal_{};
    SurfaceThermalState        thermal_{};
    WeatherGpuUniforms         gpu_{};
    std::vector<ThermalSource> thermalSources_{};

    float    simTimeSec_         = 0.0f;
    float    frontTimerSec_      = 0.0f;
    float    wetness01_          = 0.0f;
    float    puddles01_          = 0.0f;
    float    lightningCharge01_  = 0.0f;
    float    lightningFlash01_   = 0.0f;
    float    lightningCooldownS_ = 0.0f;
    uint32_t rngState_           = 1u;
};

} // namespace raw::worldgen
