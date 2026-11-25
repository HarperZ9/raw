#pragma once
// RawAudioOutput.h: Real-Time Windows WASAPI Event-Driven Background Audio Streamer &
// Deterministic NullSink Capture Backend (`raw::audio`).
// Connects RawAudioDsp (48kHz FDN-8 Hadamard Reverb + Procedural Weather/Fire/Water Soundscape)
// to low-latency shared-mode WASAPI audio output or headless lock-free ring capture.
// Zero Windows.h exposure in header, zero external library dependencies.
// Original work, MIT.

#include "RawAudioDsp.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace raw::audio {

enum class AudioBackendMode : uint8_t {
    NullSink     = 0, // Deterministic lock-free stereo ring capture (headless CI / offline WAV recording)
    WasapiShared = 1, // Windows WASAPI event-driven shared-mode float32/PCM16 output (graceful fallback if no device)
};

// Lock-free scene audio state updated from render/simulation threads at arbitrary cadence.
struct AudioSceneState {
    AcousticEnvironment environment      = AcousticEnvironment::SnowDampedPineForest;
    float               beaufortScale    = 3.0f;   // Beaufort 0..12 wind scale
    float               rain01           = 0.0f;   // Precipitation intensity [0..1]
    float               snow01           = 0.0f;   // Snowfall / snow-pack HF damping [0..1]
    float               fireIntensity01  = 0.0f;   // Nearest fire/hearth intensity [0..1]
    float               fireDistanceU    = 256.0f; // Distance to nearest fire in Skyrim world units (~70u/m)
    float               waterIntensity01 = 0.0f;   // Creek/river/ocean surf intensity [0..1]
    float               underwater01     = 0.0f;   // Submersion blend [0..1]
    float               masterGain       = 0.35f;  // Strictly clamped to [0.0 .. 0.5]
    bool                muted            = false;
};

struct WasapiDeviceInfo {
    bool        available     = false;
    std::string deviceName;
    uint32_t    mixSampleRate = 0u;
    uint16_t    mixChannels   = 0u;
    uint16_t    bitsPerSample = 0u;
    bool        isFloat       = false;
    int32_t     lastHresult   = 0;
};

struct AudioOutputStats {
    bool             running         = false;
    bool             deviceActive    = false;
    AudioBackendMode mode            = AudioBackendMode::NullSink;
    uint32_t         sampleRate      = 48000u;
    uint64_t         framesRendered  = 0ull;
    uint64_t         callbacksRun    = 0ull;
    uint64_t         ringOverruns    = 0ull;
    uint64_t         envTransitions  = 0ull;
    float            lastPeakAbs     = 0.0f;
    float            lastRms         = 0.0f;
};

// Non-intrusive probe of the default Windows WASAPI render endpoint (safe on headless CI machines).
WasapiDeviceInfo ProbeWasapiDefaultDevice();

class RawAudioOutput {
public:
    static constexpr float kMaxMasterGain   = 0.50f;
    static constexpr float kRampTauSeconds  = 0.080f; // 80 ms exponential parameter & crossfade ramp

    RawAudioOutput();
    ~RawAudioOutput();

    RawAudioOutput(const RawAudioOutput&) = delete;
    RawAudioOutput& operator=(const RawAudioOutput&) = delete;

    // Starts the background audio worker thread in WasapiShared or NullSink mode.
    // Returns true if the streamer started (note: WasapiShared degrades gracefully if no device exists).
    bool Start(AudioBackendMode mode = AudioBackendMode::NullSink,
               uint32_t preferredSampleRate = 48000u,
               uint32_t seed = 202645u);

    // Stops and cleanly joins the background worker thread (< 50 ms). Safe to call multiple times.
    void Stop();

    [[nodiscard]] bool IsRunning() const noexcept;

    // Wait-free / non-blocking update from any render or simulation thread (never blocks audio thread).
    void UpdateSceneState(const AudioSceneState& state) noexcept;
    [[nodiscard]] AudioSceneState GetSceneState() const noexcept;

    // Drains up to `maxFrames` interleaved stereo frames captured by the NullSink lock-free ring buffer.
    // Returns number of stereo frames copied into `outStereoInterleaved`.
    size_t ReadNullSinkStereo(float* outStereoInterleaved, size_t maxFrames) noexcept;

    // Returns number of stereo frames currently available in the NullSink ring buffer.
    [[nodiscard]] size_t NullSinkAvailableFrames() const noexcept;

    // Deterministic synchronous offline render pump using the exact same ramped dual-bus DSP graph
    // (safe to call when stopped, ideal for bit-exact CI/unit testing & offline WAV rendering).
    void RenderOfflineFrames(float* outStereoInterleaved, size_t frameCount);

    [[nodiscard]] AudioOutputStats GetStats() const noexcept;
    [[nodiscard]] WasapiDeviceInfo GetLastWasapiInfo() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

}  // namespace raw::audio
