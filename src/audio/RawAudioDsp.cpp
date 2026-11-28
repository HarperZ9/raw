// RawAudioDsp.cpp: First-Principles 48kHz 3D Spatial Audio, FDN-8 Hadamard Reverb &
// Procedural Weather/Fire/Water Soundscape DSP Engine (`raw::audio`).
// Original work, MIT.

#include "RawAudioDsp.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>

namespace raw::audio {
namespace {

constexpr float kPiF    = 3.14159265358979323846f;
constexpr float kTwoPiF = 6.28318530717958647692f;
constexpr float kInvSqrt8 = 0.35355339059327376220f;

// 8 mutually prime delay lengths at 48 kHz spanning 29.15 ms .. 83.10 ms
constexpr std::array<uint32_t, 8> kBasePrimeDelays48k = {
    1399u, 1667u, 1997u, 2381u, 2791u, 3187u, 3583u, 3989u
};

constexpr std::array<ReverbPreset, static_cast<size_t>(AcousticEnvironment::Count)> kPresets = {{
    {AcousticEnvironment::SnowDampedPineForest, "SnowDampedPineForest", 0.35f, 0.62f, 0.24f, 0.88f,   0.0f},
    {AcousticEnvironment::NordicStoneCrypt,     "NordicStoneCrypt",     2.85f, 0.22f, 0.48f, 0.78f,   0.0f},
    {AcousticEnvironment::GlacierIceCavern,     "GlacierIceCavern",     3.40f, 0.14f, 0.56f, 0.74f,   0.0f},
    {AcousticEnvironment::OpenWindyTundra,      "OpenWindyTundra",      0.22f, 0.42f, 0.14f, 0.94f,   0.0f},
    {AcousticEnvironment::Underwater,           "Underwater",           0.80f, 0.72f, 0.55f, 0.65f, 650.0f},
}};

inline float FlushDenormal(float x) noexcept {
    return (std::fabs(x) < 1.0e-20f || !std::isfinite(x)) ? 0.0f : x;
}

inline float ClampF(float v, float lo, float hi) noexcept {
    return v < lo ? lo : (v > hi ? hi : v);
}

inline float DotV3(AudioVec3 a, AudioVec3 b) noexcept {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline AudioVec3 SubV3(AudioVec3 a, AudioVec3 b) noexcept {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

inline float LenV3(AudioVec3 a) noexcept {
    return std::sqrt(DotV3(a, a));
}

// Fast in-place normalized 8x8 Sylvester-Hadamard transform: y = (H_8 / sqrt(8)) * x.
// Strictly orthogonal (||H_8/sqrt(8)||_2 == 1.0).
inline void ApplyUnitaryHadamard8(std::array<float, 8>& v) noexcept {
    // Stage 1 (stride 1)
    for (int i = 0; i < 8; i += 2) {
        const float a = v[size_t(i)];
        const float b = v[size_t(i + 1)];
        v[size_t(i)]     = a + b;
        v[size_t(i + 1)] = a - b;
    }
    // Stage 2 (stride 2)
    for (int i = 0; i < 8; i += 4) {
        for (int j = 0; j < 2; ++j) {
            const float a = v[size_t(i + j)];
            const float b = v[size_t(i + j + 2)];
            v[size_t(i + j)]     = a + b;
            v[size_t(i + j + 2)] = a - b;
        }
    }
    // Stage 3 (stride 4)
    for (int j = 0; j < 4; ++j) {
        const float a = v[size_t(j)];
        const float b = v[size_t(j + 4)];
        v[size_t(j)]     = (a + b) * kInvSqrt8;
        v[size_t(j + 4)] = (a - b) * kInvSqrt8;
    }
}

bool IsPrimeU32(uint32_t n) noexcept {
    if (n < 2u) return false;
    if ((n & 1u) == 0u) return n == 2u;
    for (uint32_t d = 3u; d * d <= n; d += 2u) {
        if (n % d == 0u) return false;
    }
    return true;
}

uint32_t NearestPrimeU32(uint32_t target) noexcept {
    uint32_t n = std::max(31u, target | 1u);
    while (!IsPrimeU32(n)) n += 2u;
    return n;
}

}  // namespace

const ReverbPreset& GetReverbPreset(AcousticEnvironment env) noexcept {
    const size_t idx = static_cast<size_t>(env);
    return kPresets[idx < kPresets.size() ? idx : 0u];
}

const char* GetAcousticEnvironmentName(AcousticEnvironment env) noexcept {
    return GetReverbPreset(env).name;
}

// --- Fdn8Reverb::BiquadLpf ---

void Fdn8Reverb::BiquadLpf::Configure(float cutoffHz, float sampleRate) {
    if (cutoffHz <= 10.0f || cutoffHz >= sampleRate * 0.45f) {
        enabled = false;
        b0 = 1.0f; b1 = b2 = a1 = a2 = 0.0f;
        return;
    }
    enabled = true;
    // 2-pole Butterworth low-pass (Q = 1/sqrt(2)) via Audio EQ Cookbook bilinear transform
    const float w0    = kTwoPiF * cutoffHz / sampleRate;
    const float cosW0 = std::cos(w0);
    const float sinW0 = std::sin(w0);
    const float alpha = sinW0 * 0.7071067811865475f; // sin(w0) / (2 * 0.7071)
    const float invA0 = 1.0f / (1.0f + alpha);
    b0 = ((1.0f - cosW0) * 0.5f) * invA0;
    b1 = (1.0f - cosW0) * invA0;
    b2 = b0;
    a1 = (-2.0f * cosW0) * invA0;
    a2 = (1.0f - alpha) * invA0;
}

float Fdn8Reverb::BiquadLpf::Process(float x) noexcept {
    if (!enabled) return x;
    const float y = b0 * x + z1;
    z1 = FlushDenormal(b1 * x - a1 * y + z2);
    z2 = FlushDenormal(b2 * x - a2 * y);
    return FlushDenormal(y);
}

// --- Fdn8Reverb ---

Fdn8Reverb::Fdn8Reverb(uint32_t sampleRate) {
    m_preset = GetReverbPreset(AcousticEnvironment::SnowDampedPineForest);
    SetSampleRate(sampleRate);
}

void Fdn8Reverb::SetSampleRate(uint32_t sampleRate) {
    m_sampleRate = std::clamp(sampleRate, 16000u, 192000u);
    const double scale = double(m_sampleRate) / 48000.0;
    for (uint32_t i = 0u; i < kChannels; ++i) {
        const uint32_t scaled = (m_sampleRate == 48000u)
                                  ? kBasePrimeDelays48k[i]
                                  : NearestPrimeU32(static_cast<uint32_t>(std::round(double(kBasePrimeDelays48k[i]) * scale)));
        m_delayLengths[i] = scaled;
        m_buffers[i].assign(scaled, 0.0f);
        m_writeIdx[i] = 0u;
        m_lpState[i]  = 0.0f;
    }
    m_outLpfL.Reset();
    m_outLpfR.Reset();
    UpdateCoefficients();
}

void Fdn8Reverb::SetPreset(AcousticEnvironment env) {
    m_preset = GetReverbPreset(env);
    UpdateCoefficients();
}

void Fdn8Reverb::SetCustomParams(float rt60Seconds, float hfDamping01, float wetMix01, float dryMix01, float outputLpfHz) {
    m_preset.rt60Seconds = ClampF(rt60Seconds, 0.08f, 12.0f);
    m_preset.hfDamping01 = ClampF(hfDamping01, 0.0f, 0.85f);
    m_preset.wetMix01    = ClampF(wetMix01, 0.0f, 1.0f);
    m_preset.dryMix01    = ClampF(dryMix01, 0.0f, 1.0f);
    m_preset.outputLpfHz = std::max(0.0f, outputLpfHz);
    UpdateCoefficients();
}

void Fdn8Reverb::Reset() {
    for (uint32_t i = 0u; i < kChannels; ++i) {
        std::fill(m_buffers[i].begin(), m_buffers[i].end(), 0.0f);
        m_writeIdx[i] = 0u;
        m_lpState[i]  = 0.0f;
    }
    m_outLpfL.Reset();
    m_outLpfR.Reset();
}

void Fdn8Reverb::UpdateCoefficients() {
    const double fs   = double(m_sampleRate);
    const double rt60 = std::max(0.08, double(m_preset.rt60Seconds));
    m_dampCoeff = ClampF(m_preset.hfDamping01, 0.0f, 0.85f);

    // Exact per-line RT60 feedback gain: g_i = 10^(-3 * L_i / (fs * RT60))
    for (uint32_t i = 0u; i < kChannels; ++i) {
        const double exponent = -3.0 * double(m_delayLengths[i]) / (fs * rt60);
        const double g = std::pow(10.0, exponent);
        m_feedbackGain[i] = float(std::clamp(g, 0.0, 0.9992));
    }

    m_outLpfL.Configure(m_preset.outputLpfHz, float(m_sampleRate));
    m_outLpfR.Configure(m_preset.outputLpfHz, float(m_sampleRate));
}

void Fdn8Reverb::ProcessSample(float inL, float inR, float* outL, float* outR) noexcept {
    std::array<float, kChannels> y{};

    // 1. Read delayed outputs & apply per-line ISO 9613-1 1-pole HF air-absorption low-pass + feedback gain
    const float oneMinusA = 1.0f - m_dampCoeff;
    for (uint32_t i = 0u; i < kChannels; ++i) {
        const float delayed = m_buffers[i][m_writeIdx[i]];
        const float damped  = FlushDenormal(oneMinusA * delayed + m_dampCoeff * m_lpState[i]);
        m_lpState[i] = damped;
        y[i] = FlushDenormal(damped * m_feedbackGain[i]);
    }

    // 2. Decorrelated stereo output tap extraction (balanced orthogonal signs)
    const float wetL = ( y[0] - y[1] + y[2] - y[3] + y[4] - y[5] + y[6] - y[7]) * kInvSqrt8;
    const float wetR = ( y[0] + y[1] - y[2] - y[3] + y[4] + y[5] - y[6] - y[7]) * kInvSqrt8;

    // 3. Unitary 8x8 Sylvester-Hadamard feedback mixing
    std::array<float, kChannels> mixed = y;
    ApplyUnitaryHadamard8(mixed);

    // 4. Stereo input injection across all 8 delay lines
    const float monoSum  = (inL + inR) * 0.5f;
    const float sideDiff = (inL - inR) * 0.5f;
    static constexpr float kInSignL[8] = { 1, -1,  1, -1,  1, -1,  1, -1 };
    static constexpr float kInSignR[8] = { 1,  1, -1, -1,  1,  1, -1, -1 };

    for (uint32_t i = 0u; i < kChannels; ++i) {
        const float inj = (monoSum * kInSignL[i] + sideDiff * kInSignR[i]) * kInvSqrt8;
        m_buffers[i][m_writeIdx[i]] = FlushDenormal(mixed[i] + inj);
        uint32_t nextIdx = m_writeIdx[i] + 1u;
        if (nextIdx >= m_delayLengths[i]) nextIdx = 0u;
        m_writeIdx[i] = nextIdx;
    }

    float l = inL * m_preset.dryMix01 + wetL * m_preset.wetMix01;
    float r = inR * m_preset.dryMix01 + wetR * m_preset.wetMix01;
    l = m_outLpfL.Process(l);
    r = m_outLpfR.Process(r);

    if (outL) *outL = l;
    if (outR) *outR = r;
}

void Fdn8Reverb::ProcessStereoBlock(float* stereoInterleaved, size_t frameCount) noexcept {
    if (!stereoInterleaved) return;
    for (size_t f = 0; f < frameCount; ++f) {
        float l = stereoInterleaved[f * 2u + 0u];
        float r = stereoInterleaved[f * 2u + 1u];
        ProcessSample(l, r, &stereoInterleaved[f * 2u + 0u], &stereoInterleaved[f * 2u + 1u]);
    }
}

float MeasureImpulseRt60Seconds(const std::vector<float>& monoIR, uint32_t sampleRate) {
    if (monoIR.size() < 64u || sampleRate == 0u) return 0.0f;

    // Schroeder backward integration: E[n] = sum_{k=n}^{N-1} h[k]^2
    const size_t N = monoIR.size();
    std::vector<double> schroeder(N, 0.0);
    double acc = 0.0;
    for (size_t i = N; i-- > 0u;) {
        const double s = double(monoIR[i]);
        acc += s * s;
        schroeder[i] = acc;
    }
    const double totalEnergy = schroeder[0];
    if (totalEnergy <= 1.0e-18 || !std::isfinite(totalEnergy)) return 0.0f;

    // Fit linear decay slope over the ISO 3382 T30 window: -5 dB to -35 dB (extrapolated to -60 dB)
    double sumT = 0.0, sumDb = 0.0, sumTT = 0.0, sumTDb = 0.0;
    size_t count = 0u;
    const double invFs = 1.0 / double(sampleRate);

    for (size_t i = 0; i < N; ++i) {
        const double ratio = schroeder[i] / totalEnergy;
        if (ratio <= 1.0e-9) break;
        const double db = 10.0 * std::log10(ratio);
        if (db <= -5.0 && db >= -35.0) {
            const double t = double(i) * invFs;
            sumT   += t;
            sumDb  += db;
            sumTT  += t * t;
            sumTDb += t * db;
            ++count;
        }
    }

    if (count < 8u) return 0.0f;
    const double denom = double(count) * sumTT - sumT * sumT;
    if (std::fabs(denom) < 1.0e-15) return 0.0f;
    const double slopeDbPerSec = (double(count) * sumTDb - sumT * sumDb) / denom;
    if (slopeDbPerSec >= -1.0e-3) return 0.0f;
    return float(-60.0 / slopeDbPerSec);
}

// --- Woodworth ITD/ILD 3D Spatializer + Doppler ---

SpatialAcousticParams ComputeSpatialParams(const ListenerState& listener,
                                           const SourceState& source,
                                           uint32_t sampleRate,
                                           float speedOfSoundMs,
                                           float headRadiusM) noexcept {
    SpatialAcousticParams p{};
    const float c = std::max(50.0f, speedOfSoundMs);
    const float r = std::max(0.02f, headRadiusM);

    const AudioVec3 delta = SubV3(source.position, listener.position);
    const float dist = LenV3(delta);
    p.distanceM = dist;

    AudioVec3 dir{0.0f, 1.0f, 0.0f};
    if (dist > 1.0e-5f) {
        dir = {delta.x / dist, delta.y / dist, delta.z / dist};
    }

    // Azimuth angle theta in [-pi/2 .. +pi/2], positive = source to listener's right
    const float rightLen = LenV3(listener.right);
    const AudioVec3 rightUnit = (rightLen > 1.0e-5f)
                                  ? AudioVec3{listener.right.x / rightLen, listener.right.y / rightLen, listener.right.z / rightLen}
                                  : AudioVec3{1.0f, 0.0f, 0.0f};
    const float sinAz = ClampF(DotV3(dir, rightUnit), -1.0f, 1.0f);
    const float theta = std::asin(sinAz);
    p.azimuthRad = theta;

    // Exact Woodworth spherical-head ITD formula: dt = (r / c) * (|theta| + sin(|theta|))
    const float absTheta = std::fabs(theta);
    const float magItdSec = (r / c) * (absTheta + std::sin(absTheta));
    p.itdSeconds = (theta >= 0.0f) ? magItdSec : -magItdSec;

    const float magItdSamp = magItdSec * float(sampleRate);
    if (theta >= 0.0f) {
        // Source on right: right ear arrives first, left ear delayed
        p.delayRightSamp = 0.0f;
        p.delayLeftSamp  = magItdSamp;
    } else {
        // Source on left: left ear arrives first, right ear delayed
        p.delayLeftSamp  = 0.0f;
        p.delayRightSamp = magItdSamp;
    }

    // Distance attenuation (inverse-distance law with smooth far-field rolloff)
    const float refD = std::max(0.1f, source.refDistanceM);
    const float maxD = std::max(refD + 1.0f, source.maxDistanceM);
    const float effD = std::max(refD, dist);
    const float invAtten = refD / effD;
    const float farFade  = ClampF(1.0f - (dist - refD) / (maxD - refD), 0.0f, 1.0f);
    p.distanceAtten = ClampF(invAtten * (0.25f + 0.75f * farFade), 0.0f, 1.0f);

    // Broadband + frequency-dependent ILD head-shadowing
    const float lateral = std::fabs(sinAz);
    const float contraGain = 1.0f - 0.36f * lateral; // ~ -3.9 dB broadband contralateral shadow
    const float ipsiGain   = 1.0f + 0.08f * lateral; // Subtle ipsilateral pinna gain
    const float baseGain   = std::max(0.0f, source.gain) * p.distanceAtten;

    if (theta >= 0.0f) {
        p.gainRight      = baseGain * ipsiGain;
        p.gainLeft       = baseGain * contraGain;
        p.ildShadowRight = 0.0f;
        p.ildShadowLeft  = 0.72f * lateral;
    } else {
        p.gainLeft       = baseGain * ipsiGain;
        p.gainRight      = baseGain * contraGain;
        p.ildShadowLeft  = 0.0f;
        p.ildShadowRight = 0.72f * lateral;
    }

    // Exact radial Doppler ratio: f_obs / f_src = (c + v_listener_along_dir) / (c + v_source_along_dir)
    const float vL = ClampF(DotV3(listener.velocity, dir), -c * 0.45f, c * 0.45f);
    const float vS = ClampF(DotV3(source.velocity, dir),   -c * 0.45f, c * 0.45f);
    p.dopplerRatio = ClampF((c + vL) / (c + vS), 0.5f, 2.0f);

    // ISO 9613-1 distance air-absorption low-pass cutoff
    p.airAbsorbHz = ClampF(20000.0f / (1.0f + 0.085f * dist), 1600.0f, 20000.0f);
    return p;
}

SpatialVoice3D::SpatialVoice3D(uint32_t sampleRate) : m_sampleRate(std::clamp(sampleRate, 16000u, 192000u)) {
    Reset();
}

void SpatialVoice3D::SetSampleRate(uint32_t sampleRate) {
    m_sampleRate = std::clamp(sampleRate, 16000u, 192000u);
    Reset();
}

void SpatialVoice3D::Reset() {
    m_delayL.fill(0.0f);
    m_delayR.fill(0.0f);
    m_writePos     = 0u;
    m_airLpState   = 0.0f;
    m_ildLpL       = 0.0f;
    m_ildLpR       = 0.0f;
    m_dopplerPhase = 0.0;
}

float SpatialVoice3D::ReadDelayFrac(const std::array<float, 256>& buf, float delaySamples) const noexcept {
    const float d = ClampF(delaySamples, 0.0f, 240.0f);
    const uint32_t dInt = static_cast<uint32_t>(d);
    const float frac = d - float(dInt);
    const uint32_t idx0 = (m_writePos - dInt) & kDelayMask;
    const uint32_t idx1 = (m_writePos - dInt - 1u) & kDelayMask;
    return buf[idx0] * (1.0f - frac) + buf[idx1] * frac;
}

SpatialAcousticParams SpatialVoice3D::ProcessMonoToStereo(const float* monoIn,
                                                          size_t frameCount,
                                                          const ListenerState& listener,
                                                          const SourceState& source,
                                                          float* stereoOutInterleaved,
                                                          bool accumulate) noexcept {
    const SpatialAcousticParams sp = ComputeSpatialParams(listener, source, m_sampleRate);
    if (!monoIn || !stereoOutInterleaved || frameCount == 0u) return sp;

    // 1-pole air-absorption coefficient
    const float airAlpha = ClampF(std::exp(-kTwoPiF * sp.airAbsorbHz / float(m_sampleRate)), 0.0f, 0.98f);
    const float oneMinusAir = 1.0f - airAlpha;

    // 1-pole head-shadowing shelf (~1.6 kHz)
    const float headAlpha = std::exp(-kTwoPiF * 1600.0f / float(m_sampleRate));
    const float oneMinusHead = 1.0f - headAlpha;

    for (size_t f = 0; f < frameCount; ++f) {
        const float xAir = FlushDenormal(oneMinusAir * monoIn[f] + airAlpha * m_airLpState);
        m_airLpState = xAir;

        m_delayL[m_writePos] = xAir;
        m_delayR[m_writePos] = xAir;

        const float dL = ReadDelayFrac(m_delayL, sp.delayLeftSamp);
        const float dR = ReadDelayFrac(m_delayR, sp.delayRightSamp);
        m_writePos = (m_writePos + 1u) & kDelayMask;

        // Frequency-dependent ILD: blend toward low-passed signal on shadowed ear
        m_ildLpL = FlushDenormal(oneMinusHead * dL + headAlpha * m_ildLpL);
        m_ildLpR = FlushDenormal(oneMinusHead * dR + headAlpha * m_ildLpR);

        const float earL = (dL * (1.0f - sp.ildShadowLeft)  + m_ildLpL * sp.ildShadowLeft)  * sp.gainLeft;
        const float earR = (dR * (1.0f - sp.ildShadowRight) + m_ildLpR * sp.ildShadowRight) * sp.gainRight;

        if (accumulate) {
            stereoOutInterleaved[f * 2u + 0u] += earL;
            stereoOutInterleaved[f * 2u + 1u] += earR;
        } else {
            stereoOutInterleaved[f * 2u + 0u] = earL;
            stereoOutInterleaved[f * 2u + 1u] = earR;
        }
    }
    return sp;
}

SpatialAcousticParams SpatialVoice3D::ProcessDopplerMonoToStereo(const float* monoSource,
                                                                 size_t sourceFrames,
                                                                 const ListenerState& listener,
                                                                 const SourceState& source,
                                                                 float* stereoOutInterleaved,
                                                                 size_t outFrames,
                                                                 bool accumulate) noexcept {
    const SpatialAcousticParams sp = ComputeSpatialParams(listener, source, m_sampleRate);
    if (!monoSource || sourceFrames < 2u || !stereoOutInterleaved || outFrames == 0u) return sp;

    std::vector<float> resampled(outFrames, 0.0f);
    const double step = double(sp.dopplerRatio);
    for (size_t f = 0; f < outFrames; ++f) {
        const double pos = std::fmod(m_dopplerPhase, double(sourceFrames));
        const size_t i0  = static_cast<size_t>(pos);
        const size_t i1  = (i0 + 1u < sourceFrames) ? (i0 + 1u) : 0u;
        const float frac = float(pos - double(i0));
        resampled[f] = monoSource[i0] * (1.0f - frac) + monoSource[i1] * frac;
        m_dopplerPhase += step;
    }

    return ProcessMonoToStereo(resampled.data(), outFrames, listener, source, stereoOutInterleaved, accumulate);
}

// --- SoundscapeSynth ---

uint32_t SoundscapeSynth::SplitMix32::NextU32() noexcept {
    uint32_t z = (state += 0x9E3779B9u);
    z = (z ^ (z >> 16u)) * 0x85EBCA6Bu;
    z = (z ^ (z >> 13u)) * 0xC2B2AE35u;
    return z ^ (z >> 16u);
}

float SoundscapeSynth::SplitMix32::Next01() noexcept {
    return float(NextU32() & 0x00FFFFFFu) * (1.0f / 16777216.0f);
}

float SoundscapeSynth::SplitMix32::NextSigned() noexcept {
    return Next01() * 2.0f - 1.0f;
}

float SoundscapeSynth::PinkNoise8::Next(SplitMix32& rng) noexcept {
    // Voss-McCartney 8-octave pink noise algorithm
    const uint32_t prev = counter;
    ++counter;
    const uint32_t changed = (prev ^ counter) & 0xFFu;
    for (uint32_t bit = 0u; bit < 8u; ++bit) {
        if (changed & (1u << bit)) {
            runningSum -= rows[bit];
            const float v = rng.NextSigned();
            rows[bit] = v;
            runningSum += v;
        }
    }
    const float white = rng.NextSigned();
    return (runningSum + white) * (1.0f / 9.0f);
}

void SoundscapeSynth::SvfFilter::Process(float x, float cutoffHz, float q, float sampleRate,
                                         float* outLp, float* outBp, float* outHp) noexcept {
    // Cytomic / Andy Simper TPT State-Variable Filter (unconditionally stable!)
    const float fc = ClampF(cutoffHz, 20.0f, sampleRate * 0.42f);
    const float g  = std::tan(kPiF * fc / sampleRate);
    const float k  = 1.0f / std::max(0.5f, q);
    const float a1 = 1.0f / (1.0f + g * (g + k));
    const float a2 = g * a1;
    const float a3 = g * a2;

    const float v3 = x - ic2eq;
    const float v1 = a1 * ic1eq + a2 * v3;
    const float v2 = ic2eq + a2 * ic1eq + a3 * v3;

    ic1eq = FlushDenormal(2.0f * v1 - ic1eq);
    ic2eq = FlushDenormal(2.0f * v2 - ic2eq);

    if (outLp) *outLp = v2;
    if (outBp) *outBp = v1;
    if (outHp) *outHp = x - k * v1 - v2;
}

SoundscapeSynth::SoundscapeSynth(uint32_t sampleRate, uint32_t seed)
    : m_sampleRate(std::clamp(sampleRate, 16000u, 192000u)),
      m_rng(seed),
      m_reverb(m_sampleRate) {
    Reset(seed);
}

void SoundscapeSynth::Reset(uint32_t seed) {
    m_rng = SplitMix32(seed);
    m_pinkWind.Reset();
    m_pinkWater.Reset();
    m_pinkFire.Reset();
    m_windBp1.Reset();
    m_windBp2.Reset();
    m_waterBpL.Reset();
    m_waterBpR.Reset();
    m_fireRoarLp.Reset();
    m_fireCrackBp.Reset();
    m_underwaterLpL.Reset();
    m_underwaterLpR.Reset();
    m_crackleEnv  = 0.0f;
    m_crackleFreq = 2600.0f;
    m_cracklePan  = 0.0f;
    m_phaseSec    = 0.0;
    m_reverb.Reset();
}

void SoundscapeSynth::SetSampleRate(uint32_t sampleRate) {
    m_sampleRate = std::clamp(sampleRate, 16000u, 192000u);
    m_reverb.SetSampleRate(m_sampleRate);
}

void SoundscapeSynth::SynthesizeStereo(const SoundscapeParams& params,
                                       float* outStereoInterleaved,
                                       size_t frameCount) {
    if (!outStereoInterleaved || frameCount == 0u) return;

    const AcousticEnvironment activeEnv = (params.underwaterDepthM > 0.05f)
                                            ? AcousticEnvironment::Underwater
                                            : params.environment;
    if (m_reverb.GetActivePreset().environment != activeEnv) {
        m_reverb.SetPreset(activeEnv);
    }

    const float fs       = float(m_sampleRate);
    const double dt      = 1.0 / double(m_sampleRate);
    const float windNorm = ClampF(params.windSpeedMs / 25.0f, 0.0f, 1.35f);
    const float gustAmp  = ClampF(params.windGust01, 0.0f, 1.0f);
    const float fireAmp  = ClampF(params.fireIntensity01, 0.0f, 1.0f);
    const float waterAmp = ClampF(params.waterFlow01, 0.0f, 1.0f);
    const float uwGate   = ClampF(params.underwaterDepthM * 2.0f, 0.0f, 1.0f);
    const float master   = ClampF(params.masterGain, 0.0f, 2.0f);

    // Poisson crackle trigger probability per sample (~8..35 pops/sec proportional to fireIntensity)
    const float crackleProb = (fireAmp > 0.01f) ? ((8.0f + 27.0f * fireAmp) / fs) : 0.0f;
    const float crackleDecay = std::exp(-1.0f / (0.0045f * fs)); // ~4.5 ms crisp woody pop decay

    for (size_t f = 0; f < frameCount; ++f) {
        const float t = float(m_phaseSec);
        m_phaseSec += dt;

        float mixL = 0.0f;
        float mixR = 0.0f;

        // 1. Beaufort Turbulent Wind Howl (pink noise -> dual gust-swept resonant bandpasses)
        if (windNorm > 0.01f) {
            const float gustLfo = 0.55f + 0.45f * gustAmp *
                                  (0.65f * std::sin(kTwoPiF * 0.23f * t) + 0.35f * std::sin(kTwoPiF * 0.61f * t + 1.3f));
            const float pinkW = m_pinkWind.Next(m_rng);
            const float fc1 = 160.0f + 420.0f * windNorm * gustLfo;
            const float fc2 = 340.0f + 680.0f * windNorm * (0.85f + 0.15f * std::cos(kTwoPiF * 0.37f * t));
            float bp1 = 0.0f, bp2 = 0.0f;
            m_windBp1.Process(pinkW, fc1, 2.6f + 2.0f * windNorm, fs, nullptr, &bp1, nullptr);
            m_windBp2.Process(pinkW, fc2, 3.8f + 1.8f * windNorm, fs, nullptr, &bp2, nullptr);
            const float wGain = 0.38f * windNorm * gustLfo * (1.0f - 0.85f * uwGate);
            mixL += (bp1 * 0.72f + bp2 * 0.28f) * wGain;
            mixR += (bp1 * 0.28f + bp2 * 0.72f) * wGain;
        }

        // 2. Hearth / Campfire Combustion Roar + Poisson Wood-Pop Crackle
        if (fireAmp > 0.01f) {
            const float pinkF = m_pinkFire.Next(m_rng);
            float roarLp = 0.0f;
            const float flicker = 0.78f + 0.22f * std::sin(kTwoPiF * 3.7f * t + 0.4f);
            m_fireRoarLp.Process(pinkF, 95.0f + 75.0f * fireAmp, 0.85f, fs, &roarLp, nullptr, nullptr);
            const float roar = roarLp * 0.55f * fireAmp * flicker;
            mixL += roar;
            mixR += roar;

            if (m_rng.Next01() < crackleProb) {
                m_crackleEnv  = 0.45f + 0.55f * m_rng.Next01();
                m_crackleFreq = 1600.0f + 3400.0f * m_rng.Next01();
                m_cracklePan  = m_rng.NextSigned() * 0.65f;
            }
            if (m_crackleEnv > 1.0e-4f) {
                float popBp = 0.0f;
                m_fireCrackBp.Process(m_rng.NextSigned() * m_crackleEnv, m_crackleFreq, 2.2f, fs, nullptr, &popBp, nullptr);
                const float popSig = popBp * 0.65f * fireAmp;
                mixL += popSig * (0.5f - 0.45f * m_cracklePan);
                mixR += popSig * (0.5f + 0.45f * m_cracklePan);
                m_crackleEnv *= crackleDecay;
            }
        }

        // 3. Creek / River / Ocean Surf (stereo-decorrelated modulated water turbulence)
        if (waterAmp > 0.01f) {
            const float nL = m_pinkWater.Next(m_rng);
            const float nR = m_pinkWater.Next(m_rng);
            const float surfLfo = 0.72f + 0.28f * std::sin(kTwoPiF * 0.19f * t);
            float wL = 0.0f, wR = 0.0f;
            m_waterBpL.Process(nL, 520.0f + 900.0f * waterAmp * surfLfo, 1.15f, fs, nullptr, &wL, nullptr);
            m_waterBpR.Process(nR, 580.0f + 950.0f * waterAmp * (1.0f - 0.15f * surfLfo), 1.15f, fs, nullptr, &wR, nullptr);
            const float gainW = 0.42f * waterAmp * surfLfo;
            mixL += wL * gainW;
            mixR += wR * gainW;
        }

        // 4. Underwater Muffled Hydrostatic Pressure Ambience
        if (uwGate > 0.01f) {
            const float pressNoise = m_pinkWater.Next(m_rng) * 0.45f * uwGate;
            float lpL = 0.0f, lpR = 0.0f;
            m_underwaterLpL.Process(mixL + pressNoise, 480.0f, 0.75f, fs, &lpL, nullptr, nullptr);
            m_underwaterLpR.Process(mixR + pressNoise, 480.0f, 0.75f, fs, &lpR, nullptr, nullptr);
            mixL = mixL * (1.0f - uwGate) + lpL * uwGate;
            mixR = mixR * (1.0f - uwGate) + lpR * uwGate;
        }

        // 5. FDN-8 Environment Reverb + Soft-Clipper Mastering Stage (guarantees [-1, +1] & zero clipping)
        float outL = mixL;
        float outR = mixR;
        if (params.enableReverb) {
            m_reverb.ProcessSample(mixL, mixR, &outL, &outR);
        }

        outStereoInterleaved[f * 2u + 0u] = std::tanh(FlushDenormal(outL * master)) * 0.98f;
        outStereoInterleaved[f * 2u + 1u] = std::tanh(FlushDenormal(outR * master)) * 0.98f;
    }
}

// --- 16-bit PCM RIFF/WAVE Serializer ---

std::vector<uint8_t> SerializeWavPcm16Bytes(const std::vector<float>& interleavedPcm,
                                            uint32_t sampleRate,
                                            uint16_t numChannels) {
    const uint16_t ch = std::clamp<uint16_t>(numChannels, 1u, 8u);
    const uint32_t fs = std::clamp(sampleRate, 8000u, 192000u);
    const uint32_t dataBytes = static_cast<uint32_t>(interleavedPcm.size() * sizeof(int16_t));
    const uint32_t riffChunkSize = 36u + dataBytes;
    const uint32_t byteRate = fs * uint32_t(ch) * sizeof(int16_t);
    const uint16_t blockAlign = static_cast<uint16_t>(ch * sizeof(int16_t));
    const uint16_t bitsPerSample = 16u;
    const uint32_t fmtChunkSize = 16u;
    const uint16_t audioFormatPcm = 1u;

    std::vector<uint8_t> bytes;
    bytes.resize(44u + dataBytes);

    auto writeU32 = [&](size_t off, uint32_t v) { std::memcpy(bytes.data() + off, &v, 4u); };
    auto writeU16 = [&](size_t off, uint16_t v) { std::memcpy(bytes.data() + off, &v, 2u); };

    std::memcpy(bytes.data() + 0u,  "RIFF", 4u);
    writeU32(4u,  riffChunkSize);
    std::memcpy(bytes.data() + 8u,  "WAVE", 4u);
    std::memcpy(bytes.data() + 12u, "fmt ", 4u);
    writeU32(16u, fmtChunkSize);
    writeU16(20u, audioFormatPcm);
    writeU16(22u, ch);
    writeU32(24u, fs);
    writeU32(28u, byteRate);
    writeU16(32u, blockAlign);
    writeU16(34u, bitsPerSample);
    std::memcpy(bytes.data() + 36u, "data", 4u);
    writeU32(40u, dataBytes);

    size_t off = 44u;
    for (float s : interleavedPcm) {
        const float clamped = std::isfinite(s) ? ClampF(s, -1.0f, 1.0f) : 0.0f;
        const int32_t q = static_cast<int32_t>(std::lrint(clamped * 32767.0f));
        const int16_t sample16 = static_cast<int16_t>(std::clamp(q, -32768, 32767));
        std::memcpy(bytes.data() + off, &sample16, 2u);
        off += 2u;
    }
    return bytes;
}

bool WriteWavPcm16(const std::string& path,
                   const std::vector<float>& interleavedPcm,
                   uint32_t sampleRate,
                   uint16_t numChannels) {
    if (path.empty()) return false;
    const std::vector<uint8_t> bytes = SerializeWavPcm16Bytes(interleavedPcm, sampleRate, numChannels);
    std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
    if (!ofs.is_open()) return false;
    ofs.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return ofs.good();
}

}  // namespace raw::audio
