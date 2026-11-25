// rt_stealth_survival.cpp: Wave 40 Live Creation Engine 1.7.104.0 Weather-Masked Stealth
// (SneakDetectionCalc 0x41CD90), Thermal Survival & Warmth Coupling, and 3D Acoustic Classifier.
// Original work, MIT.
#include "rt_stealth_survival.h"
#include "rt_live_config.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>

#ifndef RAW_SIM_PURE_TEST
#include "rt_detour.h"
#include "rt_image.h"
#include "rt_version.h"
#include <windows.h>
#endif

namespace raw::rt {

// =================================================================================================
// 1. Pure Deterministic Weather-Masked Stealth Resolver
// =================================================================================================
StealthDetectResult EvaluateWeatherMaskedStealth(const StealthDetectInput& in, const StealthEnvSnapshot& env) {
    StealthDetectResult r{};
    r.adjustedScore  = in.baseScore;
    r.adjustedSound  = std::isfinite(in.soundScore)  ? std::max(in.soundScore, 0.0f)  : 0.0f;
    r.adjustedVisual = std::isfinite(in.visualScore) ? std::max(in.visualScore, 0.0f) : 0.0f;
    r.soundMaskMult  = 1.0f;
    r.visualOcclMult = 1.0f;
    r.windBiasDelta  = 0.0f;

    if (!in.isExterior || !GetLiveConfig().weatherStealthEnabled) {
        // Interiors or user-disabled weather stealth leave detection untouched.
        return r;
    }

    const float rain   = std::clamp(env.rain01, 0.0f, 1.0f);
    const float snow   = std::clamp(env.snow01, 0.0f, 1.0f);
    const float storm  = std::clamp(env.storm01, 0.0f, 1.0f);
    const float fog    = std::clamp(env.fog01, 0.0f, 1.0f);
    const float night  = std::clamp(env.nightDarkness01, 0.0f, 1.0f);
    const float dist   = std::clamp(in.distanceU, 0.0f, 6000.0f);
    const float windMs = std::clamp(env.windSpeedMs, 0.0f, 40.0f);

    // A) Weather sound masking vs dry crisp sub-zero snow crunch:
    //    Heavy rain / thunderstorm / howling blizzard masks footsteps (-12% .. -35%).
    //    Dry crisp sub-zero snowpack (temp < -2C, calm wind/storm, snowDepth > 2u) amplifies crunch (+8% .. +14%).
    const float precipMask = std::max({rain * 0.30f, storm * 0.35f, snow * (windMs > 8.0f ? 0.28f : 0.14f)});
    if (precipMask >= 0.08f) {
        r.soundMaskMult = std::clamp(1.0f - std::clamp(precipMask, 0.12f, 0.35f), 0.65f, 0.88f);
    } else if (env.ambientTempC < -2.0f && env.snowDepthU > 2.0f && rain < 0.05f && storm < 0.20f) {
        const float coldFactor = std::clamp((-2.0f - env.ambientTempC) / 14.0f, 0.0f, 1.0f);
        const float depthFactor = std::clamp((env.snowDepthU - 2.0f) / 16.0f, 0.0f, 1.0f);
        r.soundMaskMult = std::clamp(1.08f + 0.06f * (0.5f * coldFactor + 0.5f * depthFactor), 1.08f, 1.14f);
    }

    // B) Atmospheric visibility occlusion (distance-scaled beyond 220u):
    //    Dense valley mist, mountain fog, coastal sea-fog, blizzard whiteout, and dark moonless nights.
    const float distW = std::clamp((dist - 180.0f) / 1100.0f, 0.0f, 1.0f);
    const float visAtten = std::clamp(fog * 0.36f + snow * 0.32f + rain * 0.14f + night * 0.22f, 0.0f, 0.68f);
    r.visualOcclMult = in.hasLOS ? std::clamp(1.0f - visAtten * (0.25f + 0.75f * distW), 0.32f, 1.0f) : 1.0f;

    // C) Wind direction acoustic/scent propagation asymmetry:
    //    Downwind observer (+1) detects slightly earlier (+2..+8); upwind observer (-1) detects later (-3..-9).
    const float windMag = std::clamp((windMs - 2.0f) / 14.0f, 0.0f, 1.0f);
    const float dotW    = std::clamp(in.obsToTgtDotWind, -1.0f, 1.0f);
    const float distFade = std::clamp(1.0f - dist / 2400.0f, 0.15f, 1.0f);
    r.windBiasDelta = dotW * windMag * distFade * (dotW >= 0.0f ? 7.5f : 9.0f);

    const float oldSound  = r.adjustedSound;
    const float oldVisual = r.adjustedVisual;
    r.adjustedSound  = oldSound * r.soundMaskMult;
    r.adjustedVisual = oldVisual * r.visualOcclMult;

    float delta = (r.adjustedSound - oldSound) + (r.adjustedVisual - oldVisual) + r.windBiasDelta;
    // If vanilla sound/visual were near zero (stationary crouch), still apply bounded weather visibility/wind bias:
    if (oldSound < 0.5f && in.isMoving && r.soundMaskMult != 1.0f) {
        delta += (r.soundMaskMult - 1.0f) * 18.0f;
    }
    if (oldVisual < 0.5f && in.hasLOS && r.visualOcclMult < 0.99f) {
        delta -= (1.0f - r.visualOcclMult) * 14.0f * distW;
    }

    const float newScoreF = static_cast<float>(in.baseScore) + std::clamp(delta, -45.0f, 20.0f);
    int32_t newScore = static_cast<int32_t>(std::lround(newScoreF));
    // Preserve Skyrim's 0x41D1E4 convention: strictly positive fractional values clamp to >= 1.
    if (newScore == 0 && newScoreF > 0.0f) newScore = 1;
    r.adjustedScore = std::clamp(newScore, -150, 200);
    return r;
}

// =================================================================================================
// 2. Pure Deterministic Thermal Body-Warmth, Wetness, Wind-Chill & Hearth Recovery
// =================================================================================================
ThermalSurvivalState StepThermalSurvival(const ThermalSurvivalState& prev, const ThermalSurvivalInput& in) {
    ThermalSurvivalState s = prev;
    if (!GetLiveConfig().thermalSurvivalEnabled) {
        s.comfortTier       = ThermalComfortTier::Comfortable;
        s.staminaRegenMult  = 1.00f;
        s.combatStaminaMult = 0.75f;
        s.staminaDelayMax   = 5.00f;
        return s;
    }
    const float dt = std::clamp(in.dtSec, 0.0f, 1.0f);
    const float windKmh = std::clamp(in.windSpeedMs, 0.0f, 45.0f) * 3.6f;

    // Effective air temperature including interior shelter & alpine altitude lapse:
    const float altLapseC = in.inInterior ? 0.0f : std::clamp(in.altitudeZ / 7000.0f, 0.0f, 14.0f);
    const float airC = in.inInterior ? std::max(in.ambientTempC, 16.0f) : (in.ambientTempC - altLapseC);

    // Physical Environment Canada / NOAA wind-chill index when exterior temp <= 10C & wind >= 4.8 km/h:
    float windChillC = airC;
    if (!in.inInterior && airC <= 10.0f && windKmh >= 4.8f) {
        const float wPow = std::pow(windKmh, 0.16f);
        windChillC = 13.12f + 0.6215f * airC - 11.37f * wPow + 0.3965f * airC * wPow;
    }
    s.effectiveWindChillC = std::clamp(windChillC, -45.0f, 40.0f);

    // Wetness accumulation (submersion / rain) & evaporation drying (hearth / interior / dry wind):
    const float hearth = in.nearHearth ? std::clamp(std::max(in.hearthProximity01, 0.55f), 0.0f, 1.0f)
                                       : std::clamp(in.hearthProximity01, 0.0f, 1.0f);
    float dWet = 0.0f;
    if (in.inWater) {
        dWet = +0.85f;
    } else if (!in.inInterior && in.rain01 > 0.08f && hearth < 0.35f) {
        dWet = +0.06f * in.rain01;
    } else {
        const float dryRate = 0.015f + hearth * 0.24f + (in.inInterior ? 0.045f : 0.008f * std::min(in.windSpeedMs, 10.0f));
        dWet = -dryRate;
    }
    s.wetness01 = std::clamp(prev.wetness01 + dWet * dt, 0.0f, 1.0f);

    // Core thermal energy balance: hearth radiant warming vs cold/wet conductive & convective heat loss.
    float targetWarmth = 82.0f;
    float rate = 0.08f;
    if (hearth > 0.10f) {
        targetWarmth = 96.0f + 4.0f * hearth;
        rate = 0.18f + 0.45f * hearth;   // standing by a fire in a blizzard warms rapidly!
    } else if (in.inInterior) {
        targetWarmth = 88.0f;
        rate = 0.14f;
    } else {
        const float coldSeverity = std::clamp((12.0f - s.effectiveWindChillC) / 34.0f, 0.0f, 1.35f);
        const float wetMult = 1.0f + s.wetness01 * 0.85f + (in.inWater ? 1.40f : 0.0f);
        const float snowPenalty = in.snow01 * 12.0f;
        targetWarmth = std::clamp(88.0f - coldSeverity * 58.0f * wetMult - snowPenalty, 8.0f, 92.0f);
        rate = 0.035f + (in.inWater ? 0.22f : 0.0f) + coldSeverity * 0.045f * wetMult;
    }
    s.coreWarmth = std::clamp(prev.coreWarmth + (targetWarmth - prev.coreWarmth) * std::min(1.0f, rate * dt), 0.0f, 100.0f);
    s.skinTempC  = std::clamp(25.5f + (s.coreWarmth / 100.0f) * 11.2f - s.wetness01 * 1.4f, 24.0f, 37.2f);

    // Non-tedious gameplay comfort tiers & stamina recovery multipliers:
    if (s.coreWarmth >= 88.0f && (hearth > 0.15f || in.inInterior)) {
        s.comfortTier       = ThermalComfortTier::WarmAndReady;
        s.staminaRegenMult  = 1.15f;
        s.combatStaminaMult = 0.86f;
        s.staminaDelayMax   = 4.25f;
    } else if (s.coreWarmth >= 64.0f) {
        s.comfortTier       = ThermalComfortTier::Comfortable;
        s.staminaRegenMult  = 1.00f;
        s.combatStaminaMult = 0.75f;
        s.staminaDelayMax   = 5.00f;
    } else if (s.coreWarmth >= 44.0f) {
        s.comfortTier       = ThermalComfortTier::Chilly;
        s.staminaRegenMult  = 0.88f;
        s.combatStaminaMult = 0.66f;
        s.staminaDelayMax   = 5.55f;
    } else if (s.coreWarmth >= 24.0f) {
        s.comfortTier       = ThermalComfortTier::Cold;
        s.staminaRegenMult  = 0.72f;
        s.combatStaminaMult = 0.56f;
        s.staminaDelayMax   = 6.15f;
    } else {
        s.comfortTier       = ThermalComfortTier::Freezing;
        s.staminaRegenMult  = 0.55f;
        s.combatStaminaMult = 0.48f;
        s.staminaDelayMax   = 6.80f;
    }
    return s;
}

// =================================================================================================
// 3. Pure Deterministic 3D Acoustic Environment Classifier
// =================================================================================================
AcousticEnvironmentProfile ClassifyAcousticEnvironment(const AcousticClassificationInput& in) {
    AcousticEnvironmentProfile p{};
    const float snow = std::clamp(in.snowCover01, 0.0f, 1.0f);
    const float rain = std::clamp(in.rain01, 0.0f, 1.0f);
    const float wind = std::clamp(in.windSpeedMs, 0.0f, 35.0f);
    const float hum  = std::clamp(in.humidity01, 0.1f, 1.0f);
    const float radM = std::clamp(in.enclosureRadiusU / 70.0f, 2.0f, 120.0f);

    // ISO 9613-1 simplified HF (8 kHz) atmospheric air absorption in dB/m (cold dry air absorbs differently than humid):
    const float baseAirDbM = std::clamp(0.0055f + (1.0f - hum) * 0.009f + std::max(0.0f, -in.temperatureC) * 0.0004f,
                                        0.004f, 0.025f);

    if (in.underwater) {
        p.kind           = AcousticEnvironmentKind::Underwater;
        p.rt60Sec        = 0.42f;
        p.earlyReflGain  = 0.30f;
        p.lowPassHz      = 650.0f;
        p.hfAirAbsorbDbM = 0.180f;
        p.reverbWetMix   = 0.65f;
        p.name           = "underwater";
        return p;
    }

    if (in.inInterior) {
        // Sabine-Eyring interior RT60 = 0.161 * V / (-S * ln(1 - alpha))
        // For spherical-equivalent room of radius R: V/S = R/3 -> RT60 ~ 0.0537 * R / (-ln(1 - alpha))
        if (in.isIceCavern || (in.worldY > 24000.0f && in.temperatureC < -1.0f)) {
            const float alpha = 0.028f;  // smooth glacier ice walls reflect strongly
            p.kind           = AcousticEnvironmentKind::GlacierIceCavern;
            p.rt60Sec        = std::clamp(0.0537f * radM / (-std::log(1.0f - alpha)), 2.2f, 4.1f);
            p.earlyReflGain  = 0.68f;
            p.lowPassHz      = 15800.0f;
            p.hfAirAbsorbDbM = baseAirDbM * 1.15f;
            p.reverbWetMix   = 0.58f;
            p.name           = "glacier_ice_cavern";
        } else {
            const float alpha = 0.045f;  // dressed Nordic stone crypt / ruin walls
            p.kind           = AcousticEnvironmentKind::NordicStoneCrypt;
            p.rt60Sec        = std::clamp(0.0537f * radM / (-std::log(1.0f - alpha)), 1.55f, 3.25f);
            p.earlyReflGain  = 0.54f;
            p.lowPassHz      = 13600.0f;
            p.hfAirAbsorbDbM = baseAirDbM * 1.05f;
            p.reverbWetMix   = 0.46f;
            p.name           = "nordic_stone_crypt";
        }
        return p;
    }

    // Exterior: Snow-damped pine forest vs Open windy tundra
    const bool pineBelt = (snow > 0.25f) || (in.worldY < -8000.0f && in.altitudeZ < 2400.0f);
    if (pineBelt && snow > 0.20f) {
        // Fresh snowpack is a potent porous acoustic absorber (alpha ~ 0.65..0.85 at mid/high frequencies):
        p.kind           = AcousticEnvironmentKind::SnowDampedPineForest;
        p.rt60Sec        = std::clamp(0.28f - snow * 0.11f, 0.15f, 0.30f);
        p.earlyReflGain  = std::clamp(0.16f - snow * 0.08f, 0.06f, 0.18f);
        p.lowPassHz      = std::clamp(14200.0f - snow * 3600.0f - wind * 95.0f, 7500.0f, 15000.0f);
        p.hfAirAbsorbDbM = std::clamp(baseAirDbM + snow * 0.028f, 0.020f, 0.052f);
        p.reverbWetMix   = 0.12f;
        p.name           = "snow_damped_pine_forest";
    } else {
        p.kind           = AcousticEnvironmentKind::OpenWindyTundra;
        p.rt60Sec        = std::clamp(0.16f + (in.altitudeZ > 2000.0f ? 0.12f : 0.0f) - wind * 0.002f, 0.11f, 0.32f);
        p.earlyReflGain  = std::clamp(0.10f + (in.altitudeZ > 2000.0f ? 0.09f : 0.0f), 0.06f, 0.24f);
        p.lowPassHz      = std::clamp(17800.0f - wind * 190.0f - rain * 2200.0f, 8800.0f, 18000.0f);
        p.hfAirAbsorbDbM = std::clamp(baseAirDbM + rain * 0.010f, 0.005f, 0.032f);
        p.reverbWetMix   = 0.08f;
        p.name           = "open_windy_tundra";
    }
    return p;
}

#ifndef RAW_SIM_PURE_TEST
// =================================================================================================
// 4. Live Creation Engine 1.7.104.0 SneakDetectionCalc (0x41CD90) Detour + Thermal GMST Feedback
// =================================================================================================
namespace {

// Exact 25-argument Windows x64 ABI proven from full disassembly of 0x41CD90..0x41D22A & caller 0x6A3972:
using SneakDetectionCalcFn = int32_t (*)(
    uint8_t* observer,            // rcx
    uint8_t* target,              // rdx
    float    observerSneakSkill,  // xmm2
    float    targetSneakSkill,    // xmm3
    int32_t  targetSize,          // [rsp+0x28]
    int32_t  hasLOS,              // [rsp+0x30]
    int32_t  visualLOSBonus,      // [rsp+0x38]
    float    distanceU,           // [rsp+0x40]
    int32_t  lightLevel,          // [rsp+0x48]
    int32_t  invisibility,        // [rsp+0x50]
    uint8_t  isBlinded,           // [rsp+0x58]
    float    equippedWeight,      // [rsp+0x60]
    uint8_t  isMoving,            // [rsp+0x68]
    uint8_t  isSneaking,          // [rsp+0x70]
    float    moveSpeed,           // [rsp+0x78]
    uint8_t  isRunning,           // [rsp+0x80]
    uint8_t  isSleeping,          // [rsp+0x88]
    uint8_t  isAlert,             // [rsp+0x90]
    int32_t  soundActionLevel,    // [rsp+0x98]
    uint8_t  isExterior,          // [rsp+0xA0]
    uint8_t  inCombat,            // [rsp+0xA8]
    int32_t  ambushState,         // [rsp+0xB0]
    float*   outSoundScore,       // [rsp+0xB8]
    float*   outVisualScore,      // [rsp+0xC0]
    float*   outSkillScore        // [rsp+0xC8]
);

struct LiveStealthSurvivalRuntime {
    SneakDetectionCalcFn origSneakCalc = nullptr;
    std::atomic<bool>    active{false};
    std::atomic<uint8_t> hookInstalled{0};
    std::atomic<uint8_t> gmstBound{0};

    std::atomic<uint32_t> sneakTotal{0};
    std::atomic<uint32_t> sneakMasked{0};
    std::atomic<uint32_t> sneakCrunch{0};

    float* pCombatStaminaMult = nullptr;
    float* pStaminaDelayMax   = nullptr;
    float  baseCombatStamina  = 0.75f;
    float  baseStaminaDelay   = 5.0f;

    std::mutex mu;
    StealthEnvSnapshot         env{};
    ThermalSurvivalState       thermal{};
    AcousticEnvironmentProfile acoustic{};
    float                      lastHearthProx = 0.0f;
} g_ss;

static bool SehReadActorPosXY(const uint8_t* actor, float* outX, float* outY) {
    if (!actor || !outX || !outY) return false;
    __try {
        *outX = *reinterpret_cast<const float*>(actor + 0x54);
        *outY = *reinterpret_cast<const float*>(actor + 0x58);
        return std::isfinite(*outX) && std::isfinite(*outY);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool SehReadFloat(const float* p, float* out) {
    if (!p || !out) return false;
    __try {
        *out = *p;
        return std::isfinite(*out);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static void SehWriteFloat(float* p, float v) {
    if (!p || !std::isfinite(v)) return;
    __try {
        *p = v;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

static int32_t HookSneakDetectionCalc(
    uint8_t* observer, uint8_t* target, float observerSneakSkill, float targetSneakSkill,
    int32_t targetSize, int32_t hasLOS, int32_t visualLOSBonus, float distanceU,
    int32_t lightLevel, int32_t invisibility, uint8_t isBlinded, float equippedWeight,
    uint8_t isMoving, uint8_t isSneaking, float moveSpeed, uint8_t isRunning,
    uint8_t isSleeping, uint8_t isAlert, int32_t soundActionLevel, uint8_t isExterior,
    uint8_t inCombat, int32_t ambushState, float* outSoundScore, float* outVisualScore, float* outSkillScore) {
    const int32_t baseScore = g_ss.origSneakCalc
        ? g_ss.origSneakCalc(observer, target, observerSneakSkill, targetSneakSkill,
                             targetSize, hasLOS, visualLOSBonus, distanceU,
                             lightLevel, invisibility, isBlinded, equippedWeight,
                             isMoving, isSneaking, moveSpeed, isRunning,
                             isSleeping, isAlert, soundActionLevel, isExterior,
                             inCombat, ambushState, outSoundScore, outVisualScore, outSkillScore)
        : 0;
    if (!g_ss.active.load(std::memory_order_acquire) || !isExterior) {
        return baseScore;
    }

    g_ss.sneakTotal.fetch_add(1u, std::memory_order_relaxed);
    StealthEnvSnapshot env{};
    {
        std::lock_guard<std::mutex> lk(g_ss.mu);
        env = g_ss.env;
    }

    float soundScore = 0.0f, visualScore = 0.0f;
    SehReadFloat(outSoundScore, &soundScore);
    SehReadFloat(outVisualScore, &visualScore);

    float dotWind = 0.0f;
    float ox = 0.0f, oy = 0.0f, tx = 0.0f, ty = 0.0f;
    if (SehReadActorPosXY(observer, &ox, &oy) && SehReadActorPosXY(target, &tx, &ty)) {
        const float dx = ox - tx, dy = oy - ty;
        const float len = std::sqrt(dx * dx + dy * dy);
        if (len > 1.0f) {
            dotWind = (dx / len) * env.windDirX + (dy / len) * env.windDirY;
        }
    }

    StealthDetectInput in{};
    in.baseScore       = baseScore;
    in.soundScore      = soundScore;
    in.visualScore     = visualScore;
    in.distanceU       = distanceU;
    in.isExterior      = (isExterior != 0u);
    in.isMoving        = (isMoving != 0u);
    in.hasLOS          = (hasLOS > 0);
    in.obsToTgtDotWind = dotWind;

    const StealthDetectResult res = EvaluateWeatherMaskedStealth(in, env);
    if (res.soundMaskMult < 0.99f) g_ss.sneakMasked.fetch_add(1u, std::memory_order_relaxed);
    if (res.soundMaskMult > 1.01f) g_ss.sneakCrunch.fetch_add(1u, std::memory_order_relaxed);

    if (outSoundScore)  SehWriteFloat(outSoundScore, res.adjustedSound);
    if (outVisualScore) SehWriteFloat(outVisualScore, res.adjustedVisual);
    return res.adjustedScore;
}

}  // namespace

std::string StealthSurvivalInstallHooks(const Image& img) {
    if (!AreBinaryOffsetsVerified()) {
        return "[StealthSurvival] REFUSED (fail-closed: startup Zydis binary offset verification did not pass)";
    }
    if (ActiveProfile().isMultiplayerOnline || ActiveProfile().gen != raw::versions::EngineGen::CE1_DX11) {
        return "[StealthSurvival] skipped (non-CE1 or online profile)";
    }

    // Bind stamina regen GMSTs for thermal comfort/exposure feedback:
    const uint32_t rvaCombStam = Addr("fCombatStaminaRegenRateMult");
    const uint32_t rvaDelayMax = Addr("fStaminaRegenDelayMax");
    g_ss.pCombatStaminaMult = rvaCombStam ? reinterpret_cast<float*>(const_cast<uint8_t*>(img.base) + rvaCombStam + 8u) : nullptr;
    g_ss.pStaminaDelayMax   = rvaDelayMax ? reinterpret_cast<float*>(const_cast<uint8_t*>(img.base) + rvaDelayMax + 8u) : nullptr;
    if (g_ss.pCombatStaminaMult) SehReadFloat(g_ss.pCombatStaminaMult, &g_ss.baseCombatStamina);
    if (g_ss.pStaminaDelayMax)   SehReadFloat(g_ss.pStaminaDelayMax,   &g_ss.baseStaminaDelay);
    g_ss.gmstBound.store((g_ss.pCombatStaminaMult || g_ss.pStaminaDelayMax) ? 1u : 0u, std::memory_order_release);

    const uint32_t rvaSneak = Addr("fn:SneakDetectionCalc");
    bool hooked = false;
    std::string detMsg = "missing";
    if (rvaSneak) {
        detMsg = InstallDetour(img, rvaSneak,
                               reinterpret_cast<void*>(&HookSneakDetectionCalc),
                               reinterpret_cast<void**>(&g_ss.origSneakCalc));
        hooked = (detMsg.rfind("ok:", 0) == 0);
    }
    g_ss.hookInstalled.store(hooked ? 1u : 0u, std::memory_order_release);
    g_ss.active.store(hooked || g_ss.gmstBound.load(std::memory_order_relaxed) != 0u, std::memory_order_release);

    char buf[256];
    std::snprintf(buf, sizeof buf,
                  "[StealthSurvival] SneakDetectionCalc(0x%X)=%s (%s) thermalGmstBound=%u",
                  rvaSneak, hooked ? "ACTIVE" : "inactive", detMsg.c_str(),
                  g_ss.gmstBound.load(std::memory_order_relaxed));
    return buf;
}

void StealthSurvivalShutdown() {
    g_ss.active.store(false, std::memory_order_release);
    if (g_ss.pCombatStaminaMult) SehWriteFloat(g_ss.pCombatStaminaMult, g_ss.baseCombatStamina);
    if (g_ss.pStaminaDelayMax)   SehWriteFloat(g_ss.pStaminaDelayMax,   g_ss.baseStaminaDelay);
}

void StealthSurvivalUpdateFrame(const StealthEnvSnapshot& env,
                                const ThermalSurvivalInput& thermalIn,
                                const AcousticClassificationInput& acousticIn) {
    ThermalSurvivalState nextTherm{};
    AcousticEnvironmentProfile nextAc{};
    {
        std::lock_guard<std::mutex> lk(g_ss.mu);
        g_ss.env = env;
        nextTherm = StepThermalSurvival(g_ss.thermal, thermalIn);
        nextAc    = ClassifyAcousticEnvironment(acousticIn);
        g_ss.thermal        = nextTherm;
        g_ss.acoustic       = nextAc;
        g_ss.lastHearthProx = thermalIn.hearthProximity01;
    }
    if (g_ss.active.load(std::memory_order_acquire) && g_ss.gmstBound.load(std::memory_order_relaxed)) {
        const bool thermalOn = GetLiveConfig().thermalSurvivalEnabled;
        if (g_ss.pCombatStaminaMult) {
            SehWriteFloat(g_ss.pCombatStaminaMult, thermalOn ? nextTherm.combatStaminaMult : g_ss.baseCombatStamina);
        }
        if (g_ss.pStaminaDelayMax) {
            SehWriteFloat(g_ss.pStaminaDelayMax, thermalOn ? nextTherm.staminaDelayMax : g_ss.baseStaminaDelay);
        }
    }
}

bool StealthSurvivalGetTelemetry(StealthSurvivalTelemetry* out) {
    if (!out) return false;
    std::lock_guard<std::mutex> lk(g_ss.mu);
    out->sneakHookActive  = g_ss.hookInstalled.load(std::memory_order_relaxed);
    out->staminaGmstBound = g_ss.gmstBound.load(std::memory_order_relaxed);
    out->comfortTier      = static_cast<uint8_t>(g_ss.thermal.comfortTier);
    out->acousticEnvKind  = static_cast<uint8_t>(g_ss.acoustic.kind);
    out->sneakChecksTotal = g_ss.sneakTotal.load(std::memory_order_relaxed);
    out->sneakMaskedCount = g_ss.sneakMasked.load(std::memory_order_relaxed);
    out->sneakCrunchCount = g_ss.sneakCrunch.load(std::memory_order_relaxed);
    out->coreWarmth       = g_ss.thermal.coreWarmth;
    out->wetness01        = g_ss.thermal.wetness01;
    out->windChillC       = g_ss.thermal.effectiveWindChillC;
    out->staminaRegenMult = g_ss.thermal.staminaRegenMult;
    out->hearthProximity01 = g_ss.lastHearthProx;
    out->acousticRt60Sec  = g_ss.acoustic.rt60Sec;
    out->acousticHfAbsorb = g_ss.acoustic.hfAirAbsorbDbM;
    return true;
}

std::string StealthSurvivalStatsJson() {
    StealthSurvivalTelemetry t{};
    StealthSurvivalGetTelemetry(&t);
    char b[320];
    std::snprintf(b, sizeof b,
                  "\"sneak_hook\":%u,\"sneak_checks\":%u,\"sneak_masked\":%u,\"sneak_crunch\":%u,"
                  "\"warmth\":%.1f,\"wetness\":%.2f,\"wind_chill_c\":%.1f,\"stam_regen_mult\":%.2f,"
                  "\"hearth_prox\":%.2f,\"acoustic_env\":%u,\"rt60\":%.2f,\"hf_absorb_dbm\":%.4f",
                  t.sneakHookActive, t.sneakChecksTotal, t.sneakMaskedCount, t.sneakCrunchCount,
                  t.coreWarmth, t.wetness01, t.windChillC, t.staminaRegenMult,
                  t.hearthProximity01, t.acousticEnvKind, t.acousticRt60Sec, t.acousticHfAbsorb);
    return b;
}
#endif

}  // namespace raw::rt
