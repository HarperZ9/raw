// test_audio_dsp.cpp: First-Principles 48kHz FDN-8 Reverb, Woodworth 3D Spatializer &
// Procedural Soundscape DSP Tests.
// Linkable into ce_runtime_check via raw::audio::RunAudioDspTests(checkFn),
// or buildable standalone with -DRAW_AUDIO_DSP_STANDALONE_MAIN.
// Original work, MIT.

#include "../src/audio/RawAudioDsp.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace raw::audio {
namespace {

constexpr uint32_t kFs = 48000u;
constexpr float    kTwoPi = 6.28318530717958647692f;

// Measures Schroeder RT60 and verifies BIBO decay below -60 dB for a given FDN-8 environment preset.
struct IrCheckResult {
    float rt60Sec     = 0.0f;
    float tailDb      = 0.0f;
    bool  allFinite   = true;
    bool  decayed60Db = false;
};

IrCheckResult MeasurePresetIr(AcousticEnvironment env) {
    Fdn8Reverb rev(kFs);
    rev.SetPreset(env);
    // Use 100% wet mix for pure FDN-8 reverb tail measurement (keeps exact preset RT60 & damping!)
    const ReverbPreset p = rev.GetActivePreset();
    rev.SetCustomParams(p.rt60Seconds, p.hfDamping01, 1.0f, 0.0f, p.outputLpfHz);

    // Record for max(1.5s, 1.8 * targetRT60) so even 3.4s GlacierIceCavern decays well past -60 dB
    const float durationSec = std::max(1.5f, p.rt60Seconds * 1.85f);
    const size_t totalFrames = static_cast<size_t>(std::ceil(durationSec * float(kFs)));
    std::vector<float> monoIR(totalFrames, 0.0f);

    IrCheckResult res{};
    double earlyEnergy = 0.0;
    double lateEnergy  = 0.0;
    const size_t earlyEnd = std::min<size_t>(totalFrames, static_cast<size_t>(0.12f * float(kFs)));
    const size_t lateStart = totalFrames - std::min<size_t>(totalFrames / 8u, static_cast<size_t>(0.12f * float(kFs)));

    for (size_t i = 0; i < totalFrames; ++i) {
        const float imp = (i == 0u) ? 1.0f : 0.0f;
        float oL = 0.0f, oR = 0.0f;
        rev.ProcessSample(imp, imp, &oL, &oR);
        if (!std::isfinite(oL) || !std::isfinite(oR)) {
            res.allFinite = false;
            return res;
        }
        const float m = 0.5f * (oL + oR);
        monoIR[i] = m;
        if (i < earlyEnd)   earlyEnergy += double(m) * double(m);
        if (i >= lateStart) lateEnergy  += double(m) * double(m);
    }

    res.rt60Sec = MeasureImpulseRt60Seconds(monoIR, kFs);
    const double ratio = (earlyEnergy > 1.0e-18) ? std::max(lateEnergy / earlyEnergy, 1.0e-16) : 1.0;
    res.tailDb = float(10.0 * std::log10(ratio));
    res.decayed60Db = (res.tailDb < -60.0f);
    return res;
}

uint32_t CountPositiveZeroCrossings(const std::vector<float>& stereo, size_t channel, size_t startFrame, size_t endFrame) {
    uint32_t crossings = 0u;
    for (size_t f = startFrame + 1u; f < endFrame; ++f) {
        const float prev = stereo[(f - 1u) * 2u + channel];
        const float curr = stereo[f * 2u + channel];
        if (prev <= 0.0f && curr > 0.0f) ++crossings;
    }
    return crossings;
}

}  // namespace

int RunAudioDspTests(void (*checkFn)(bool, const char*)) {
    int failures = 0;
    auto check = [&](bool ok, const char* name) {
        if (!ok) ++failures;
        if (checkFn) {
            checkFn(ok, name);
        } else {
            std::printf("%s %s\n", ok ? "PASS" : "FAIL", name);
        }
    };

    // 1. FDN-8 Schroeder RT60 across all 5 environments + BIBO stability decay < -60 dB
    const IrCheckResult irTundra  = MeasurePresetIr(AcousticEnvironment::OpenWindyTundra);
    const IrCheckResult irForest  = MeasurePresetIr(AcousticEnvironment::SnowDampedPineForest);
    const IrCheckResult irUnder   = MeasurePresetIr(AcousticEnvironment::Underwater);
    const IrCheckResult irCrypt   = MeasurePresetIr(AcousticEnvironment::NordicStoneCrypt);
    const IrCheckResult irGlacier = MeasurePresetIr(AcousticEnvironment::GlacierIceCavern);

    if (!checkFn) {
        std::printf("[FDN-8 RT60] Tundra=%.3fs (%.1fdB) | Forest=%.3fs (%.1fdB) | Underwater=%.3fs (%.1fdB) | Crypt=%.3fs (%.1fdB) | Glacier=%.3fs (%.1fdB)\n",
                    double(irTundra.rt60Sec), double(irTundra.tailDb),
                    double(irForest.rt60Sec), double(irForest.tailDb),
                    double(irUnder.rt60Sec),  double(irUnder.tailDb),
                    double(irCrypt.rt60Sec),  double(irCrypt.tailDb),
                    double(irGlacier.rt60Sec), double(irGlacier.tailDb));
    }

    const bool allIrFinite  = irTundra.allFinite && irForest.allFinite && irUnder.allFinite && irCrypt.allFinite && irGlacier.allFinite;
    const bool allIrDecayed = irTundra.decayed60Db && irForest.decayed60Db && irUnder.decayed60Db && irCrypt.decayed60Db && irGlacier.decayed60Db;
    const bool rt60Ordered  = (irGlacier.rt60Sec > irCrypt.rt60Sec) &&
                              (irCrypt.rt60Sec   > irForest.rt60Sec) &&
                              (irForest.rt60Sec  > irTundra.rt60Sec) &&
                              (irUnder.rt60Sec   > irForest.rt60Sec) &&
                              (irGlacier.rt60Sec > 2.2f) &&
                              (irTundra.rt60Sec  > 0.08f && irTundra.rt60Sec < 0.45f);

    check(allIrFinite,  "audio_fdn8_all_5_environments_zero_nan_inf");
    check(allIrDecayed, "audio_fdn8_all_5_environments_bibo_decay_below_minus_60db");
    check(rt60Ordered,  "audio_fdn8_schroeder_rt60_strict_environment_ordering");

    // 2. Underwater 650 Hz Butterworth LPF check (passes 180 Hz, strongly attenuates 4000 Hz)
    {
        Fdn8Reverb uwRev(kFs);
        uwRev.SetPreset(AcousticEnvironment::Underwater);
        uwRev.SetCustomParams(0.80f, 0.72f, 0.0f, 1.0f, 650.0f);

        auto measureToneRms = [&](float freqHz) -> float {
            uwRev.Reset();
            double sumSq = 0.0;
            constexpr size_t N = 4800u;
            for (size_t i = 0; i < N; ++i) {
                const float s = std::sin(kTwoPi * freqHz * float(i) / float(kFs));
                float oL = 0.0f, oR = 0.0f;
                uwRev.ProcessSample(s, s, &oL, &oR);
                if (i >= 1000u) sumSq += double(oL) * double(oL);
            }
            return float(std::sqrt(sumSq / double(N - 1000u)));
        };

        const float rmsLow  = measureToneRms(180.0f);
        const float rmsHigh = measureToneRms(4000.0f);
        const float attenDb = 20.0f * std::log10(std::max(rmsHigh, 1.0e-7f) / std::max(rmsLow, 1.0e-7f));
        check(rmsLow > 0.55f && attenDb < -24.0f, "audio_underwater_650hz_lpf_attenuates_hf");
    }

    // 3. Binaural Woodworth ITD/ILD 3D Spatializer polarity & magnitude verification
    {
        ListenerState listener{};
        SourceState srcRight{};
        srcRight.position = {4.0f, 0.0f, 0.0f}; // Pure +90 deg right

        SourceState srcLeft{};
        srcLeft.position = {-4.0f, 0.0f, 0.0f}; // Pure -90 deg left

        SourceState srcFront{};
        srcFront.position = {0.0f, 4.0f, 0.0f}; // Dead center front

        const SpatialAcousticParams pR = ComputeSpatialParams(listener, srcRight, kFs);
        const SpatialAcousticParams pL = ComputeSpatialParams(listener, srcLeft,  kFs);
        const SpatialAcousticParams pF = ComputeSpatialParams(listener, srcFront, kFs);

        // Expected Woodworth max ITD at +/-90 deg: (0.0875 / 343.0) * (pi/2 + 1) ~= 0.6557 ms ~= 31.47 samples
        const float expectedItdSec = (0.0875f / 343.0f) * (1.5707963f + 1.0f);
        const bool woodworthMathOk =
            std::fabs(pR.itdSeconds - expectedItdSec) < 1.0e-5f &&
            std::fabs(pL.itdSeconds + expectedItdSec) < 1.0e-5f &&
            std::fabs(pF.itdSeconds) < 1.0e-6f &&
            pR.delayRightSamp == 0.0f && pR.delayLeftSamp > 28.0f && pR.delayLeftSamp < 35.0f &&
            pL.delayLeftSamp  == 0.0f && pL.delayRightSamp > 28.0f && pL.delayRightSamp < 35.0f &&
            pR.gainRight > pR.gainLeft &&
            pL.gainLeft  > pL.gainRight &&
            std::fabs(pF.gainLeft - pF.gainRight) < 1.0e-5f;
        check(woodworthMathOk, "audio_woodworth_itd_ild_polarity_and_magnitude");

        // Actual rendered impulse arrival-sample lag check on right-side source
        SpatialVoice3D voice(kFs);
        std::vector<float> imp(128u, 0.0f);
        imp[10u] = 1.0f;
        std::vector<float> stereo(256u, 0.0f);
        voice.ProcessMonoToStereo(imp.data(), imp.size(), listener, srcRight, stereo.data(), false);

        size_t peakL = 0u, peakR = 0u;
        float maxL = -1.0f, maxR = -1.0f;
        for (size_t i = 0; i < imp.size(); ++i) {
            if (std::fabs(stereo[i * 2u + 0u]) > maxL) { maxL = std::fabs(stereo[i * 2u + 0u]); peakL = i; }
            if (std::fabs(stereo[i * 2u + 1u]) > maxR) { maxR = std::fabs(stereo[i * 2u + 1u]); peakR = i; }
        }
        const int measuredLag = int(peakL) - int(peakR);
        check(measuredLag >= 29 && measuredLag <= 34 && maxR > maxL,
              "audio_spatial_voice_rendered_itd_lag_matches_woodworth");
    }

    // 4. Radial Doppler pitch ratio (approaching > 1.0 > receding) + rendered frequency shift check
    {
        ListenerState listener{};
        SourceState srcApproach{};
        srcApproach.position = {0.0f, 12.0f, 0.0f};
        srcApproach.velocity = {0.0f, -34.3f, 0.0f}; // Moving toward listener at Mach 0.1

        SourceState srcRecede{};
        srcRecede.position = {0.0f, 12.0f, 0.0f};
        srcRecede.velocity = {0.0f, +34.3f, 0.0f}; // Moving away from listener at Mach 0.1

        const SpatialAcousticParams pApp = ComputeSpatialParams(listener, srcApproach, kFs);
        const SpatialAcousticParams pRec = ComputeSpatialParams(listener, srcRecede,   kFs);
        const bool ratioOk =
            std::fabs(pApp.dopplerRatio - (343.0f / (343.0f - 34.3f))) < 1.0e-3f &&
            std::fabs(pRec.dopplerRatio - (343.0f / (343.0f + 34.3f))) < 1.0e-3f &&
            pApp.dopplerRatio > 1.08f && pRec.dopplerRatio < 0.93f;
        check(ratioOk, "audio_doppler_analytic_approaching_gt_receding");

        // Render a 500 Hz mono tone through SpatialVoice3D Doppler resampler and verify pitch shift
        constexpr size_t kToneFrames = 9600u; // 200 ms
        std::vector<float> tone(kToneFrames, 0.0f);
        for (size_t i = 0; i < kToneFrames; ++i) {
            tone[i] = std::sin(kTwoPi * 500.0f * float(i) / float(kFs));
        }
        std::vector<float> outApp(kToneFrames * 2u, 0.0f);
        std::vector<float> outRec(kToneFrames * 2u, 0.0f);

        SpatialVoice3D vApp(kFs), vRec(kFs);
        vApp.ProcessDopplerMonoToStereo(tone.data(), tone.size(), listener, srcApproach, outApp.data(), kToneFrames);
        vRec.ProcessDopplerMonoToStereo(tone.data(), tone.size(), listener, srcRecede,   outRec.data(), kToneFrames);

        const uint32_t zcApp = CountPositiveZeroCrossings(outApp, 0u, 480u, kToneFrames - 480u);
        const uint32_t zcRec = CountPositiveZeroCrossings(outRec, 0u, 480u, kToneFrames - 480u);
        const float renderedRatio = float(zcApp) / float(std::max(1u, zcRec));
        const float expectedRatio = pApp.dopplerRatio / pRec.dopplerRatio;
        check(zcApp > zcRec && std::fabs(renderedRatio - expectedRatio) < 0.05f,
              "audio_doppler_rendered_waveform_pitch_ratio");
    }

    // 5. 48,000-sample (1.0 s) multi-source procedural soundscape + spatial mix + deterministic check + RIFF/WAVE export
    {
        SoundscapeParams sp{};
        sp.environment     = AcousticEnvironment::SnowDampedPineForest;
        sp.windSpeedMs     = 14.0f;
        sp.windGust01      = 0.65f;
        sp.fireIntensity01 = 0.85f;
        sp.waterFlow01     = 0.55f;
        sp.masterGain      = 0.85f;
        sp.enableReverb    = true;
        sp.seed            = 202644u;

        std::vector<float> mixA(size_t(kFs) * 2u, 0.0f);
        std::vector<float> mixB(size_t(kFs) * 2u, 0.0f);

        SoundscapeSynth synthA(kFs, sp.seed);
        synthA.SynthesizeStereo(sp, mixA.data(), kFs);

        SoundscapeSynth synthB(kFs, sp.seed);
        synthB.SynthesizeStereo(sp, mixB.data(), kFs);

        bool allFinite = true;
        float peakAbs = 0.0f;
        double sumSq = 0.0;
        for (float s : mixA) {
            if (!std::isfinite(s)) { allFinite = false; break; }
            peakAbs = std::max(peakAbs, std::fabs(s));
            sumSq += double(s) * double(s);
        }
        const float rms = float(std::sqrt(sumSq / double(mixA.size())));

        if (!checkFn) {
            std::printf("[Soundscape 48kHz 1s] rms=%.4f peak=%.4f finite=%s\n",
                        double(rms), double(peakAbs), allFinite ? "YES" : "NO");
        }

        check(allFinite && peakAbs <= 1.0f && rms > 0.01f && rms < 0.75f,
              "audio_soundscape_48k_multisource_bounded_rms_zero_clip");
        check(mixA == mixB, "audio_soundscape_bit_exact_deterministic_seed");

        // RIFF/WAVE 16-bit PCM serialization check
        const std::vector<uint8_t> wav = SerializeWavPcm16Bytes(mixA, kFs, 2u);
        const size_t expectedBytes = 44u + size_t(kFs) * 2u * 2u;
        uint32_t riffSize = 0u, sampleRate = 0u, dataSize = 0u;
        uint16_t audioFmt = 0u, channels = 0u, bitsPerSamp = 0u;
        if (wav.size() == expectedBytes) {
            std::memcpy(&riffSize,    wav.data() + 4u,  4u);
            std::memcpy(&audioFmt,    wav.data() + 20u, 2u);
            std::memcpy(&channels,    wav.data() + 22u, 2u);
            std::memcpy(&sampleRate,  wav.data() + 24u, 4u);
            std::memcpy(&bitsPerSamp, wav.data() + 34u, 2u);
            std::memcpy(&dataSize,    wav.data() + 40u, 4u);
        }
        const bool wavOk =
            wav.size() == expectedBytes &&
            std::memcmp(wav.data() + 0u,  "RIFF", 4u) == 0 &&
            std::memcmp(wav.data() + 8u,  "WAVE", 4u) == 0 &&
            std::memcmp(wav.data() + 12u, "fmt ", 4u) == 0 &&
            std::memcmp(wav.data() + 36u, "data", 4u) == 0 &&
            riffSize == (expectedBytes - 8u) &&
            audioFmt == 1u && channels == 2u && sampleRate == kFs && bitsPerSamp == 16u &&
            dataSize == (expectedBytes - 44u);
        check(wavOk, "audio_wav_pcm16_riff_header_and_payload_valid");
    }

    if (!checkFn) {
        std::printf("AudioDspTests completed: %s (failures=%d)\n",
                    failures == 0 ? "PASS" : "FAIL", failures);
    }
    return failures;
}

}  // namespace raw::audio

#ifdef RAW_AUDIO_DSP_STANDALONE_MAIN
int main() {
    const int failures = raw::audio::RunAudioDspTests(nullptr);
    return failures == 0 ? 0 : 1;
}
#endif
