#pragma once
// rt_audio_bridge.h: Wave 46 Opt-In Hot-Reloadable Live WASAPI 48kHz FDN-8 Ambient Soundscape Bridge.
// Connects rt_world_sim (5-environment acoustics, Beaufort wind, rain/snow), rt_stealth_survival
// (nearby hearth warmth), rt_view (water submersion, interior flag, menu pause auto-mute), and
// rt_live_config (raw_engine.ini [Audio] LiveAudioAmbience=0, LiveAudioGain=0.22) to RawAudioOutput's
// wait-free SeqLock mailbox with zero mutex contention on the real-time audio thread.
// Original work, MIT.

#include "../audio/RawAudioOutput.h"

#include <cstdint>
#include <string>

namespace raw::rt {

struct AudioBridgeInput {
    uint8_t acousticEnvClass  = 0u;     // AcousticEnvironmentKind (0=Tundra,1=Forest,2=Crypt,3=Glacier,4=Underwater)
    float   beaufortScale     = 2.0f;   // Beaufort 0..12 wind scale
    float   windSpeedMs       = 0.0f;   // Fallback wind speed (m/s) when beaufortScale <= 0
    float   rain01            = 0.0f;   // [0..1]
    float   snow01            = 0.0f;   // [0..1]
    float   hearthProximity01 = 0.0f;   // [0..1] nearby fire/hearth/brazier warmth
    bool    inWater           = false;  // Player submerged / swimming
    bool    inInterior        = false;  // Interior cell (attenuates exterior wind)
    bool    menuPaused        = false;  // Pause/map/inventory menu active -> auto-mutes smoothly
    bool    liveAudioEnabled  = false;  // From raw_engine.ini [Audio] LiveAudioAmbience (default 0 = OFF)
    float   liveAudioGain     = 0.22f;  // From raw_engine.ini [Audio] LiveAudioGain [0.00..0.50]
};

struct AudioBridgeTelemetry {
    bool     initialized      = false;
    bool     enabled          = false;
    bool     streamRunning    = false;
    bool     deviceActive     = false;
    bool     muted            = true;
    uint8_t  mappedDspEnv     = 0u;
    float    appliedGain      = 0.0f;
    uint32_t toggleStarts     = 0u;
    uint32_t toggleStops      = 0u;
    uint64_t framesRendered   = 0ull;
    uint64_t envTransitions   = 0ull;
    float    lastRms          = 0.0f;
};

// Maps rt_world_sim / rt_stealth_survival AcousticEnvironmentKind (0..4) to RawAudioDsp::AcousticEnvironment.
raw::audio::AcousticEnvironment MapRuntimeAcousticEnvToDsp(uint8_t runtimeEnvClass) noexcept;

// Pure deterministic conversion from runtime simulation snapshot to wait-free AudioSceneState.
raw::audio::AudioSceneState BuildAudioSceneStateFromRuntime(const AudioBridgeInput& in) noexcept;

// Deterministic hot-toggleable controller (used live with WasapiShared and in CI tests with NullSink).
class AudioBridgeController {
public:
    void Init(raw::audio::AudioBackendMode backend = raw::audio::AudioBackendMode::WasapiShared,
              uint32_t sampleRate = 48000u,
              uint32_t seed = 202646u);

    // Evaluates hot-toggle transitions (0 -> 1 starts stream, 1 -> 0 cleanly stops & joins < 5 ms)
    // and pushes updated AudioSceneState into the wait-free SeqLock mailbox.
    void Tick(const AudioBridgeInput& input);

    // Stops and joins the background audio thread cleanly (< 5 ms).
    void Shutdown();

    [[nodiscard]] AudioBridgeTelemetry GetTelemetry() const noexcept;
    [[nodiscard]] raw::audio::RawAudioOutput& Output() noexcept { return m_output; }
    [[nodiscard]] const raw::audio::RawAudioOutput& Output() const noexcept { return m_output; }

private:
    raw::audio::RawAudioOutput   m_output;
    raw::audio::AudioBackendMode m_backend       = raw::audio::AudioBackendMode::WasapiShared;
    uint32_t                     m_sampleRate    = 48000u;
    uint32_t                     m_seed          = 202646u;
    bool                         m_initialized   = false;
    bool                         m_enabled       = false;
    uint32_t                     m_toggleStarts  = 0u;
    uint32_t                     m_toggleStops   = 0u;
    raw::audio::AudioSceneState  m_lastScene{};
};

#ifndef RAW_SIM_PURE_TEST
// Live raw_engine.dll lifecycle hooks (opt-in via raw_engine.ini [Audio] LiveAudioAmbience=1).
std::string AudioBridgeInit();
void        AudioBridgeOnPresent();
void        AudioBridgeShutdown();
bool        AudioBridgeGetTelemetry(AudioBridgeTelemetry* out);
#endif

}  // namespace raw::rt
