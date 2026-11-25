// rt_audio_bridge.cpp: Wave 46 Opt-In Hot-Reloadable Live WASAPI 48kHz FDN-8 Ambient Soundscape Bridge.
// Original work, MIT.

#include "rt_audio_bridge.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#ifndef RAW_SIM_PURE_TEST
#include "rt_live_config.h"
#include "rt_stealth_survival.h"
#include "rt_view.h"
#include "rt_world_sim.h"
#endif

namespace raw::rt {
namespace {

inline float ClampF(float v, float lo, float hi) noexcept {
    if (!std::isfinite(v)) return lo;
    return v < lo ? lo : (v > hi ? hi : v);
}

// Inverse Beaufort mapping when only windSpeedMs is populated: B = (v / 0.836)^(2/3).
inline float WindMsToBeaufort(float windMs) noexcept {
    const float v = std::max(0.0f, std::isfinite(windMs) ? windMs : 0.0f);
    if (v <= 0.01f) return 0.0f;
    return ClampF(std::pow(v / 0.836f, 2.0f / 3.0f), 0.0f, 12.0f);
}

#ifndef RAW_SIM_PURE_TEST
AudioBridgeController g_audioBridge;
#endif

}  // namespace

raw::audio::AcousticEnvironment MapRuntimeAcousticEnvToDsp(uint8_t runtimeEnvClass) noexcept {
    // AcousticEnvironmentKind (rt_stealth_survival.h / rt_world_sim.h):
    //   0 = OpenWindyTundra
    //   1 = SnowDampedPineForest
    //   2 = NordicStoneCrypt
    //   3 = GlacierIceCavern
    //   4 = Underwater
    switch (runtimeEnvClass) {
        case 0u:  return raw::audio::AcousticEnvironment::OpenWindyTundra;
        case 1u:  return raw::audio::AcousticEnvironment::SnowDampedPineForest;
        case 2u:  return raw::audio::AcousticEnvironment::NordicStoneCrypt;
        case 3u:  return raw::audio::AcousticEnvironment::GlacierIceCavern;
        case 4u:  return raw::audio::AcousticEnvironment::Underwater;
        default:  return raw::audio::AcousticEnvironment::SnowDampedPineForest;
    }
}

raw::audio::AudioSceneState BuildAudioSceneStateFromRuntime(const AudioBridgeInput& in) noexcept {
    raw::audio::AudioSceneState s{};
    s.environment = in.inWater
                        ? raw::audio::AcousticEnvironment::Underwater
                        : MapRuntimeAcousticEnvToDsp(in.acousticEnvClass);

    float beaufort = (in.beaufortScale > 0.01f) ? ClampF(in.beaufortScale, 0.0f, 12.0f)
                                                : WindMsToBeaufort(in.windSpeedMs);
    if (in.inInterior) {
        // Muffle exterior wind inside crypts, taverns, and caverns
        beaufort *= 0.22f;
    }
    s.beaufortScale = beaufort;

    s.rain01 = in.inInterior ? (ClampF(in.rain01, 0.0f, 1.0f) * 0.25f) : ClampF(in.rain01, 0.0f, 1.0f);
    s.snow01 = in.inInterior ? 0.0f : ClampF(in.snow01, 0.0f, 1.0f);

    const float hearth = ClampF(in.hearthProximity01, 0.0f, 1.0f);
    s.fireIntensity01  = hearth;
    // Map hearth proximity [0..1] to Skyrim world units: 1.0 -> 64u (~0.9m), 0.0 -> 576u (~8.2m)
    s.fireDistanceU    = 64.0f + (1.0f - hearth) * 512.0f;

    s.underwater01     = in.inWater ? 1.0f : 0.0f;
    s.waterIntensity01 = ClampF(std::max(s.rain01 * 0.65f, in.inWater ? 0.85f : 0.0f), 0.0f, 1.0f);
    s.masterGain       = ClampF(in.liveAudioGain, 0.0f, raw::audio::RawAudioOutput::kMaxMasterGain);
    s.muted            = in.menuPaused || !in.liveAudioEnabled;
    return s;
}

void AudioBridgeController::Init(raw::audio::AudioBackendMode backend, uint32_t sampleRate, uint32_t seed) {
    Shutdown();
    m_backend     = backend;
    m_sampleRate  = sampleRate;
    m_seed        = seed;
    m_enabled     = false;
    m_toggleStarts = 0u;
    m_toggleStops  = 0u;
    m_lastScene   = {};
    m_initialized = true;
}

void AudioBridgeController::Tick(const AudioBridgeInput& input) {
    if (!m_initialized) return;

    const raw::audio::AudioSceneState scene = BuildAudioSceneStateFromRuntime(input);
    m_lastScene = scene;

    if (input.liveAudioEnabled) {
        m_output.UpdateSceneState(scene);
        if (!m_enabled) {
            m_enabled = true;
            ++m_toggleStarts;
            m_output.Start(m_backend, m_sampleRate, m_seed);
        }
    } else if (m_enabled) {
        m_enabled = false;
        ++m_toggleStops;
        m_output.UpdateSceneState(scene);
        m_output.Stop();
    }
}

void AudioBridgeController::Shutdown() {
    if (m_output.IsRunning() || m_enabled) {
        m_enabled = false;
        m_output.Stop();
    }
    m_initialized = false;
}

AudioBridgeTelemetry AudioBridgeController::GetTelemetry() const noexcept {
    AudioBridgeTelemetry t{};
    const raw::audio::AudioOutputStats st = m_output.GetStats();
    t.initialized    = m_initialized;
    t.enabled        = m_enabled;
    t.streamRunning  = st.running;
    t.deviceActive   = st.deviceActive;
    t.muted          = m_lastScene.muted;
    t.mappedDspEnv   = static_cast<uint8_t>(m_lastScene.environment);
    t.appliedGain    = m_lastScene.masterGain;
    t.toggleStarts   = m_toggleStarts;
    t.toggleStops    = m_toggleStops;
    t.framesRendered = st.framesRendered;
    t.envTransitions = st.envTransitions;
    t.lastRms        = st.lastRms;
    return t;
}

#ifndef RAW_SIM_PURE_TEST
std::string AudioBridgeInit() {
    g_audioBridge.Init(raw::audio::AudioBackendMode::WasapiShared, 48000u, 202646u);
    const LiveEngineConfig cfg = GetLiveConfig();
    char buf[192];
    std::snprintf(buf, sizeof buf,
                  "[AudioBridge] initialized (backend=WASAPI-Shared, optIn=%u, gain=%.2f, hot-reloadable via raw_engine.ini [Audio])",
                  cfg.liveAudioAmbience ? 1u : 0u, cfg.liveAudioGain);
    return buf;
}

void AudioBridgeOnPresent() {
    const LiveEngineConfig cfg = GetLiveConfig();

    AudioBridgeInput in{};
    in.liveAudioEnabled = cfg.liveAudioAmbience;
    in.liveAudioGain    = cfg.liveAudioGain;

    // Fast path when disabled and already stopped: zero extra telemetry reads!
    if (!cfg.liveAudioAmbience && !g_audioBridge.GetTelemetry().enabled) {
        return;
    }

    RawWorldSimTelemetry ws{};
    if (RAW_GetWorldSimTelemetry(&ws)) {
        in.acousticEnvClass = ws.acousticEnvClass;
        in.beaufortScale    = ws.beaufortScale;
        in.windSpeedMs      = ws.windSpeed;
        in.rain01           = ws.synopticRain;
        in.snow01           = ws.synopticSnow;
    }

    StealthSurvivalTelemetry ss{};
    if (StealthSurvivalGetTelemetry(&ss)) {
        in.hearthProximity01 = ss.hearthProximity01;
        if (ws.simTicks == 0u) {
            in.acousticEnvClass = ss.acousticEnvKind;
        }
    }

    EngineView ev{};
    if (RAW_EngineView(&ev) > 0u) {
        in.inInterior = (ev.interior != 0u);
        in.menuPaused = (ev.menuPaused != 0u);
        // Player head (~95u above feet) below valid cell waterHeight OR Underwater acoustic class
        const bool waterValid = std::isfinite(ev.waterHeight) && (ev.waterHeight > -1.0e6f) && (ev.waterHeight < 1.0e6f);
        const bool headSubmerged = waterValid && ((ev.playerPos[2] + 95.0f) < ev.waterHeight);
        in.inWater = headSubmerged || (in.acousticEnvClass == 4u);
    } else {
        in.inWater = (in.acousticEnvClass == 4u);
    }

    g_audioBridge.Tick(in);
}

void AudioBridgeShutdown() {
    g_audioBridge.Shutdown();
}

bool AudioBridgeGetTelemetry(AudioBridgeTelemetry* out) {
    if (!out) return false;
    *out = g_audioBridge.GetTelemetry();
    return out->initialized;
}
#endif

}  // namespace raw::rt
