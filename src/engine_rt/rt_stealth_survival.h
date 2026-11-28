#pragma once
// rt_stealth_survival.h: Wave 40 Live Creation Engine 1.7.104.0 Weather-Masked Stealth
// (SneakDetectionCalc 0x41CD90), Thermal Body-Warmth / Hearth Survival, and 3D Acoustic Classifier.
// 100% binary-verified, Zydis-checked, fail-closed, SEH-guarded (Zero SKSE).
// Original work, MIT.

#include <cstdint>
#include <string>

namespace raw::rt {
struct Image;

// =================================================================================================
// 1. Weather-Masked Stealth & Wind-Propagation Detection Resolver (mirrors 0x41CD90 decomposition)
// =================================================================================================
struct StealthEnvSnapshot {
    float rain01           = 0.0f;    // 0..1 live rain intensity
    float snow01           = 0.0f;    // 0..1 live snowfall / blizzard intensity
    float storm01          = 0.0f;    // 0..1 combined storm / thunder intensity
    float fog01            = 0.0f;    // 0..1 valley mist / mountain fog / sea-fog density
    float snowDepthU       = 0.0f;    // ground snowpack depth (Skyrim units)
    float ambientTempC     = 8.0f;    // live ambient air temperature (C)
    float nightDarkness01  = 0.0f;    // 0 = bright noon, 1 = dark moonless night
    float windDirX         = 1.0f;    // normalized 2D wind travel direction X
    float windDirY         = 0.0f;    // normalized 2D wind travel direction Y
    float windSpeedMs      = 3.0f;    // live wind speed (m/s)
};

struct StealthDetectInput {
    int32_t baseScore      = 0;       // vanilla int32 return of SneakDetectionCalc (0x41CD90)
    float   soundScore     = 0.0f;    // vanilla *outSoundScore
    float   visualScore    = 0.0f;    // vanilla *outVisualScore
    float   distanceU      = 400.0f;  // distance between observer and target (units)
    bool    isExterior     = true;    // Arg 20 of 0x41CD90
    bool    isMoving       = false;   // Arg 13 of 0x41CD90
    bool    hasLOS         = true;    // Arg 6 > 0
    float   obsToTgtDotWind = 0.0f;   // dot(normalize(observer.xy - target.xy), windDir.xy): +1 = observer downwind
};

struct StealthDetectResult {
    int32_t adjustedScore   = 0;
    float   adjustedSound   = 0.0f;
    float   adjustedVisual  = 0.0f;
    float   soundMaskMult   = 1.0f;   // <1 rain/blizzard mask, >1 crisp sub-zero snow crunch
    float   visualOcclMult  = 1.0f;   // <1 fog/blizzard/dark-night occlusion
    float   windBiasDelta   = 0.0f;   // >0 downwind detection boost, <0 upwind masking
};

StealthDetectResult EvaluateWeatherMaskedStealth(const StealthDetectInput& in, const StealthEnvSnapshot& env);

// =================================================================================================
// 2. Live Thermal Body-Warmth, Wetness, Wind-Chill & Hearth Recovery Model
// =================================================================================================
enum class ThermalComfortTier : uint8_t {
    WarmAndReady = 0,   // Near hearth / comfortable: +15% stamina regen bonus
    Comfortable  = 1,   // Nominal 1.0x
    Chilly       = 2,   // Mild chill: 0.88x
    Cold         = 3,   // Shivering: 0.72x
    Freezing     = 4,   // Severe Arctic / icy water exposure: 0.55x
};

struct ThermalSurvivalInput {
    float ambientTempC      = 8.0f;
    float windSpeedMs       = 3.0f;
    float rain01            = 0.0f;
    float snow01            = 0.0f;
    float altitudeZ         = 0.0f;
    bool  inInterior        = false;
    bool  inWater           = false;
    bool  nearHearth        = false;
    float hearthProximity01 = 0.0f;   // 0 (far) .. 1 (right beside campfire/forge/brazier)
    float dtSec             = 1.0f / 30.0f;
};

struct ThermalSurvivalState {
    float              coreWarmth         = 82.0f;   // 0..100
    float              skinTempC          = 33.5f;   // 24..37 C
    float              effectiveWindChillC = 8.0f;
    float              wetness01          = 0.0f;    // 0 (dry) .. 1 (soaked)
    float              staminaRegenMult   = 1.0f;    // 0.55..1.15
    float              combatStaminaMult  = 0.75f;   // applied to fCombatStaminaRegenRateMult (0.48..0.86)
    float              staminaDelayMax    = 5.0f;    // applied to fStaminaRegenDelayMax (4.2..6.8)
    ThermalComfortTier comfortTier        = ThermalComfortTier::Comfortable;
};

ThermalSurvivalState StepThermalSurvival(const ThermalSurvivalState& prev, const ThermalSurvivalInput& in);

// =================================================================================================
// 3. 3D Physical Acoustic Environment Classifier (Sabine-Eyring RT60 + ISO 9613-1 Air Absorption)
// =================================================================================================
enum class AcousticEnvironmentKind : uint8_t {
    OpenWindyTundra      = 0,
    SnowDampedPineForest = 1,
    NordicStoneCrypt     = 2,
    GlacierIceCavern     = 3,
    Underwater           = 4,
};

struct AcousticClassificationInput {
    bool  inInterior        = false;
    bool  underwater        = false;
    bool  isIceCavern       = false;
    float altitudeZ         = 0.0f;
    float worldY            = 0.0f;
    float snowCover01       = 0.0f;
    float rain01            = 0.0f;
    float windSpeedMs       = 3.0f;
    float temperatureC      = 6.0f;
    float humidity01        = 0.55f;
    float enclosureRadiusU  = 1200.0f;
};

struct AcousticEnvironmentProfile {
    AcousticEnvironmentKind kind            = AcousticEnvironmentKind::OpenWindyTundra;
    float                   rt60Sec         = 0.18f;
    float                   earlyReflGain   = 0.12f;
    float                   lowPassHz       = 17500.0f;
    float                   hfAirAbsorbDbM  = 0.006f;
    float                   reverbWetMix    = 0.10f;
    const char*             name            = "open_windy_tundra";
};

AcousticEnvironmentProfile ClassifyAcousticEnvironment(const AcousticClassificationInput& in);

// =================================================================================================
// 4. Live Engine Runtime Hook Lifecycle & Telemetry
// =================================================================================================
struct StealthSurvivalTelemetry {
    uint8_t  sneakHookActive    = 0;
    uint8_t  staminaGmstBound   = 0;
    uint8_t  comfortTier        = 1;
    uint8_t  acousticEnvKind    = 0;
    uint32_t sneakChecksTotal   = 0;
    uint32_t sneakMaskedCount   = 0;
    uint32_t sneakCrunchCount   = 0;
    float    coreWarmth         = 82.0f;
    float    wetness01          = 0.0f;
    float    windChillC         = 8.0f;
    float    staminaRegenMult   = 1.0f;
    float    hearthProximity01  = 0.0f;
    float    acousticRt60Sec    = 0.18f;
    float    acousticHfAbsorb   = 0.006f;
};

#ifndef RAW_SIM_PURE_TEST
std::string StealthSurvivalInstallHooks(const Image& img);
void        StealthSurvivalShutdown();
void        StealthSurvivalUpdateFrame(const StealthEnvSnapshot& env,
                                       const ThermalSurvivalInput& thermalIn,
                                       const AcousticClassificationInput& acousticIn);
bool        StealthSurvivalGetTelemetry(StealthSurvivalTelemetry* out);
std::string StealthSurvivalStatsJson();
#endif

}  // namespace raw::rt
