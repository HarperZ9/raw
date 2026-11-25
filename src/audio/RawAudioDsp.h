#pragma once
// RawAudioDsp.h: First-Principles 48kHz 3D Spatial Audio, FDN-8 Hadamard Reverb &
// Procedural Weather/Fire/Water Soundscape DSP Engine (`raw::audio`).
// Standalone clean-room MIT C++20/23 framework with zero external dependencies.
// Features:
//   - 8-channel orthogonal Sylvester-Hadamard (H_8 / sqrt(8)) Feedback Delay Network (Fdn8Reverb)
//     with mutually prime 29..83 ms delay lines, exact RT60 feedback gains, ISO 9613-1 1-pole HF
//     air absorption damping, denormal flushing, and 5 live environment presets
//   - Binaural Woodworth spherical-head ITD fractional delay + ILD head-shadowing shelf +
//     radial Doppler pitch ratio + inverse-distance atmospheric attenuation (SpatialVoice3D)
//   - Procedural physical soundscape synthesizer (Beaufort wind howl, Poisson campfire crackle &
//     combustion roar, modulated creek/surf water, underwater pressure ambience) + 16-bit PCM WAV writer
// Original work, MIT.

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace raw::audio {

struct AudioVec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

// Synchronized with RAW's live 5-environment acoustics classifier.
enum class AcousticEnvironment : uint8_t {
    SnowDampedPineForest = 0, // RT60 = 0.35 s, strong HF snow absorption
    NordicStoneCrypt     = 1, // RT60 = 2.85 s, reflective masonry
    GlacierIceCavern     = 2, // RT60 = 3.40 s, bright crystalline ice
    OpenWindyTundra      = 3, // RT60 = 0.22 s, open-air short slapback
    Underwater           = 4, // RT60 = 0.80 s + 650 Hz Butterworth low-pass
    Count                = 5,
};

struct ReverbPreset {
    AcousticEnvironment environment  = AcousticEnvironment::SnowDampedPineForest;
    const char*         name         = "SnowDampedPineForest";
    float               rt60Seconds  = 0.35f;
    float               hfDamping01  = 0.62f; // [0..0.85] ISO 9613-1 1-pole HF absorption
    float               wetMix01     = 0.24f;
    float               dryMix01     = 0.88f;
    float               outputLpfHz  = 0.0f;  // >0 enables 2-pole Butterworth output low-pass (e.g. 650 Hz underwater)
};

const ReverbPreset& GetReverbPreset(AcousticEnvironment env) noexcept;
const char*         GetAcousticEnvironmentName(AcousticEnvironment env) noexcept;

// 8-Channel Unitary Sylvester-Hadamard Feedback Delay Network Reverb.
class Fdn8Reverb {
public:
    static constexpr uint32_t kChannels = 8u;

    explicit Fdn8Reverb(uint32_t sampleRate = 48000u);

    void SetSampleRate(uint32_t sampleRate);
    void SetPreset(AcousticEnvironment env);
    void SetCustomParams(float rt60Seconds, float hfDamping01, float wetMix01, float dryMix01, float outputLpfHz = 0.0f);
    void Reset();

    // Processes one stereo sample pair in-place or into outL/outR.
    void ProcessSample(float inL, float inR, float* outL, float* outR) noexcept;

    // Processes interleaved stereo buffer [L0, R0, L1, R1, ...] in-place.
    void ProcessStereoBlock(float* stereoInterleaved, size_t frameCount) noexcept;

    [[nodiscard]] uint32_t GetSampleRate() const noexcept { return m_sampleRate; }
    [[nodiscard]] const ReverbPreset& GetActivePreset() const noexcept { return m_preset; }
    [[nodiscard]] const std::array<uint32_t, kChannels>& GetDelayLengths() const noexcept { return m_delayLengths; }

private:
    void UpdateCoefficients();

    struct BiquadLpf {
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
        float z1 = 0.0f, z2 = 0.0f;
        bool  enabled = false;
        void  Configure(float cutoffHz, float sampleRate);
        void  Reset() noexcept { z1 = 0.0f; z2 = 0.0f; }
        float Process(float x) noexcept;
    };

    uint32_t                             m_sampleRate = 48000u;
    ReverbPreset                         m_preset{};
    std::array<uint32_t, kChannels>      m_delayLengths{};
    std::array<std::vector<float>, kChannels> m_buffers;
    std::array<uint32_t, kChannels>      m_writeIdx{};
    std::array<float, kChannels>         m_feedbackGain{};
    std::array<float, kChannels>         m_lpState{};
    float                                m_dampCoeff = 0.45f;
    BiquadLpf                            m_outLpfL{};
    BiquadLpf                            m_outLpfR{};
};

// Measures ISO 3382 Schroeder backward-integrated RT60 (from -5 dB to -35 dB linear fit) of a mono IR.
float MeasureImpulseRt60Seconds(const std::vector<float>& monoIR, uint32_t sampleRate);

// --- Binaural Woodworth ITD/ILD 3D Spatializer + Doppler + Atmospheric Attenuation ---

struct ListenerState {
    AudioVec3 position{0.0f, 0.0f, 0.0f};
    AudioVec3 velocity{0.0f, 0.0f, 0.0f};
    AudioVec3 forward{0.0f, 1.0f, 0.0f};  // Unit forward look vector (Z-up)
    AudioVec3 right{1.0f, 0.0f, 0.0f};    // Unit right ear axis
    AudioVec3 up{0.0f, 0.0f, 1.0f};
};

struct SourceState {
    AudioVec3 position{0.0f, 2.0f, 0.0f};
    AudioVec3 velocity{0.0f, 0.0f, 0.0f};
    float     gain         = 1.0f;
    float     refDistanceM = 1.5f;
    float     maxDistanceM = 120.0f;
};

struct SpatialAcousticParams {
    float distanceM       = 1.0f;
    float azimuthRad      = 0.0f; // [-pi/2 .. +pi/2], + = right of listener, - = left of listener
    float itdSeconds      = 0.0f; // Signed: >0 means left ear delayed (source on right); <0 means right ear delayed
    float delayLeftSamp   = 0.0f; // >= 0 fractional samples delayed for left ear
    float delayRightSamp  = 0.0f; // >= 0 fractional samples delayed for right ear
    float gainLeft        = 1.0f; // Broadband + ILD ear gain (left)
    float gainRight       = 1.0f; // Broadband + ILD ear gain (right)
    float ildShadowLeft   = 0.0f; // [0..1] HF head-shadow strength on left ear
    float ildShadowRight  = 0.0f; // [0..1] HF head-shadow strength on right ear
    float dopplerRatio    = 1.0f; // Observed frequency / source frequency (>1 approaching, <1 receding)
    float distanceAtten   = 1.0f; // Inverse-distance attenuation factor [0..1]
    float airAbsorbHz     = 18000.0f;
};

// Pure physical Woodworth spherical-head ITD/ILD + radial Doppler + distance attenuation evaluation.
SpatialAcousticParams ComputeSpatialParams(const ListenerState& listener,
                                           const SourceState& source,
                                           uint32_t sampleRate = 48000u,
                                           float speedOfSoundMs = 343.0f,
                                           float headRadiusM = 0.0875f) noexcept;

class SpatialVoice3D {
public:
    explicit SpatialVoice3D(uint32_t sampleRate = 48000u);

    void SetSampleRate(uint32_t sampleRate);
    void Reset();

    // Spatializes a mono input signal into interleaved stereo output (accumulating into outStereo if accumulate=true).
    // Returns the computed physical spatial parameters for telemetry/testing.
    SpatialAcousticParams ProcessMonoToStereo(const float* monoIn,
                                              size_t frameCount,
                                              const ListenerState& listener,
                                              const SourceState& source,
                                              float* stereoOutInterleaved,
                                              bool accumulate = false) noexcept;

    // Resamples a mono source buffer using the radial Doppler ratio and spatializes to stereo.
    SpatialAcousticParams ProcessDopplerMonoToStereo(const float* monoSource,
                                                     size_t sourceFrames,
                                                     const ListenerState& listener,
                                                     const SourceState& source,
                                                     float* stereoOutInterleaved,
                                                     size_t outFrames,
                                                     bool accumulate = false) noexcept;

private:
    static constexpr uint32_t kDelayMask = 255u; // 256-sample fractional ITD delay ring (>5.3 ms at 48 kHz)

    float ReadDelayFrac(const std::array<float, 256>& buf, float delaySamples) const noexcept;

    uint32_t                m_sampleRate = 48000u;
    std::array<float, 256>  m_delayL{};
    std::array<float, 256>  m_delayR{};
    uint32_t                m_writePos = 0u;
    float                   m_airLpState = 0.0f;
    float                   m_ildLpL     = 0.0f;
    float                   m_ildLpR     = 0.0f;
    double                  m_dopplerPhase = 0.0;
};

// --- Procedural Physical Weather / Fire / Water / Underwater Soundscape Synthesizer ---

struct SoundscapeParams {
    AcousticEnvironment environment     = AcousticEnvironment::SnowDampedPineForest;
    float               windSpeedMs     = 7.5f;  // Beaufort wind speed (0..32 m/s)
    float               windGust01      = 0.45f; // Turbulence gustiness [0..1]
    float               fireIntensity01 = 0.0f;  // Hearth/campfire combustion & wood crackle [0..1]
    float               waterFlow01     = 0.0f;  // Creek/river/surf turbulence [0..1]
    float               underwaterDepthM = 0.0f; // >0 engages underwater hydrostatic rumble + 650 Hz LPF
    float               masterGain      = 0.85f;
    bool                enableReverb    = true;
    uint32_t            seed            = 202644u;
};

class SoundscapeSynth {
public:
    explicit SoundscapeSynth(uint32_t sampleRate = 48000u, uint32_t seed = 202644u);

    void Reset(uint32_t seed = 202644u);
    void SetSampleRate(uint32_t sampleRate);

    // Synthesizes `frameCount` interleaved stereo frames (2 * frameCount floats) strictly bounded in [-1, 1].
    void SynthesizeStereo(const SoundscapeParams& params, float* outStereoInterleaved, size_t frameCount);

    [[nodiscard]] uint32_t GetSampleRate() const noexcept { return m_sampleRate; }
    [[nodiscard]] const Fdn8Reverb& GetReverb() const noexcept { return m_reverb; }

private:
    struct SplitMix32 {
        uint32_t state = 0x9E3779B9u;
        explicit SplitMix32(uint32_t s = 202644u) : state(s ? s : 0x9E3779B9u) {}
        uint32_t NextU32() noexcept;
        float    NextSigned() noexcept; // [-1, +1]
        float    Next01() noexcept;     // [0, 1)
    };

    struct PinkNoise8 {
        std::array<float, 8> rows{};
        float                runningSum = 0.0f;
        uint32_t             counter    = 0u;
        void  Reset() noexcept { rows.fill(0.0f); runningSum = 0.0f; counter = 0u; }
        float Next(SplitMix32& rng) noexcept;
    };

    struct SvfFilter {
        float ic1eq = 0.0f;
        float ic2eq = 0.0f;
        void  Reset() noexcept { ic1eq = 0.0f; ic2eq = 0.0f; }
        void  Process(float x, float cutoffHz, float q, float sampleRate,
                      float* outLp, float* outBp, float* outHp) noexcept;
    };

    uint32_t   m_sampleRate = 48000u;
    SplitMix32 m_rng{};
    PinkNoise8 m_pinkWind{};
    PinkNoise8 m_pinkWater{};
    PinkNoise8 m_pinkFire{};
    SvfFilter  m_windBp1{};
    SvfFilter  m_windBp2{};
    SvfFilter  m_waterBpL{};
    SvfFilter  m_waterBpR{};
    SvfFilter  m_fireRoarLp{};
    SvfFilter  m_fireCrackBp{};
    SvfFilter  m_underwaterLpL{};
    SvfFilter  m_underwaterLpR{};
    float      m_crackleEnv  = 0.0f;
    float      m_crackleFreq = 2600.0f;
    float      m_cracklePan  = 0.0f;
    double     m_phaseSec    = 0.0;
    Fdn8Reverb m_reverb;
};

// Serializes interleaved float PCM [-1..+1] into a standard 44-byte RIFF/WAVE 16-bit PCM byte buffer.
std::vector<uint8_t> SerializeWavPcm16Bytes(const std::vector<float>& interleavedPcm,
                                            uint32_t sampleRate = 48000u,
                                            uint16_t numChannels = 2u);

// Writes interleaved float PCM [-1..+1] to a standard 16-bit RIFF/WAVE `.wav` file on disk.
bool WriteWavPcm16(const std::string& path,
                   const std::vector<float>& interleavedPcm,
                   uint32_t sampleRate = 48000u,
                   uint16_t numChannels = 2u);

}  // namespace raw::audio
