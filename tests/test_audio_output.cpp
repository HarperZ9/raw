// test_audio_output.cpp: Real-Time WASAPI / NullSink Background Audio Streamer Tests.
// Linkable into ce_runtime_check via raw::audio::RunAudioOutputTests(checkFn),
// or buildable standalone with -DRAW_AUDIO_OUTPUT_STANDALONE_MAIN.
// Original work, MIT.

#include "../src/audio/RawAudioOutput.h"
#include "../src/engine_rt/rt_audio_bridge.h"
#include "../src/engine_rt/rt_live_config.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <limits>
#include <thread>
#include <vector>

namespace raw::audio {
namespace {

struct WaveMetrics {
    bool  allFinite     = true;
    float peakAbs       = 0.0f;
    float rms           = 0.0f;
    float maxSampleJump = 0.0f;
};

WaveMetrics AnalyzeStereoWave(const std::vector<float>& stereo, size_t startFrame, size_t endFrame) {
    WaveMetrics m{};
    if (endFrame <= startFrame || endFrame * 2u > stereo.size()) return m;

    double sumSq = 0.0;
    for (size_t f = startFrame; f < endFrame; ++f) {
        const float l = stereo[f * 2u + 0u];
        const float r = stereo[f * 2u + 1u];
        if (!std::isfinite(l) || !std::isfinite(r)) {
            m.allFinite = false;
            return m;
        }
        m.peakAbs = std::max(m.peakAbs, std::max(std::fabs(l), std::fabs(r)));
        sumSq += double(l) * double(l) + double(r) * double(r);
        if (f > startFrame) {
            const float jL = std::fabs(l - stereo[(f - 1u) * 2u + 0u]);
            const float jR = std::fabs(r - stereo[(f - 1u) * 2u + 1u]);
            m.maxSampleJump = std::max(m.maxSampleJump, std::max(jL, jR));
        }
    }
    m.rms = float(std::sqrt(sumSq / double((endFrame - startFrame) * 2u)));
    return m;
}

}  // namespace

int RunAudioOutputTests(void (*checkFn)(bool, const char*)) {
    int failures = 0;
    auto check = [&](bool ok, const char* name) {
        if (!ok) ++failures;
        if (checkFn) {
            checkFn(ok, name);
        } else {
            std::printf("%s %s\n", ok ? "PASS" : "FAIL", name);
        }
    };

    // 1. Mailbox safety clamping & NaN guard (masterGain strictly <= 0.50)
    {
        RawAudioOutput out;
        AudioSceneState dirty{};
        dirty.masterGain    = 99.0f;
        dirty.beaufortScale = 50.0f;
        dirty.rain01        = std::numeric_limits<float>::quiet_NaN();
        dirty.fireDistanceU = -10.0f;
        out.UpdateSceneState(dirty);

        const AudioSceneState clean = out.GetSceneState();
        const bool clampedOk =
            clean.masterGain <= RawAudioOutput::kMaxMasterGain + 1.0e-6f &&
            clean.masterGain >= 0.49f &&
            clean.beaufortScale <= 12.0f &&
            clean.rain01 == 0.0f &&
            clean.fireDistanceU >= 16.0f;
        check(clampedOk, "audio_output_mailbox_clamps_gain_and_sanitizes_nan");
    }

    // 2. Deterministic multi-environment transition test: OpenWindyTundra -> GlacierIceCavern -> Underwater
    //    Verifies zero clicks/pops (maxSampleJump < 0.25), finite bounded RMS, zero clipping, and 2 transitions.
    {
        RawAudioOutput out;
        constexpr size_t kSegFrames = 12000u; // 250 ms per segment @ 48 kHz
        std::vector<float> fullWave(kSegFrames * 3u * 2u, 0.0f);

        AudioSceneState s{};
        s.environment      = AcousticEnvironment::OpenWindyTundra;
        s.beaufortScale    = 8.5f;
        s.fireIntensity01  = 0.85f;
        s.fireDistanceU    = 120.0f;
        s.waterIntensity01 = 0.60f;
        s.underwater01     = 0.0f;
        s.masterGain       = 0.48f;
        s.muted            = false;
        out.UpdateSceneState(s);
        out.RenderOfflineFrames(fullWave.data(), kSegFrames);

        // Transition 1: OpenWindyTundra -> GlacierIceCavern
        s.environment     = AcousticEnvironment::GlacierIceCavern;
        s.beaufortScale   = 5.0f;
        s.fireIntensity01 = 0.90f;
        out.UpdateSceneState(s);
        out.RenderOfflineFrames(fullWave.data() + kSegFrames * 2u, kSegFrames);

        // Transition 2: GlacierIceCavern -> Underwater
        s.environment   = AcousticEnvironment::Underwater;
        s.underwater01  = 1.0f;
        s.waterIntensity01 = 0.85f;
        out.UpdateSceneState(s);
        out.RenderOfflineFrames(fullWave.data() + kSegFrames * 2u * 2u, kSegFrames);

        const WaveMetrics wm = AnalyzeStereoWave(fullWave, 0u, kSegFrames * 3u);
        const AudioOutputStats st = out.GetStats();

        if (!checkFn) {
            std::printf("[EnvTransitions] frames=%llu transitions=%llu rms=%.4f peak=%.4f maxJump=%.4f\n",
                        static_cast<unsigned long long>(st.framesRendered),
                        static_cast<unsigned long long>(st.envTransitions),
                        double(wm.rms), double(wm.peakAbs), double(wm.maxSampleJump));
        }

        check(wm.allFinite && wm.peakAbs <= 1.0f && wm.rms > 0.005f && wm.rms < 0.75f,
              "audio_output_env_transitions_bounded_rms_zero_clip");
        check(wm.maxSampleJump < 0.25f && st.envTransitions >= 2ull,
              "audio_output_env_transitions_zero_clicks_pops");
    }

    // 3. Mute / Unmute smooth 80 ms exponential ramp test
    {
        RawAudioOutput out;
        AudioSceneState s{};
        s.environment      = AcousticEnvironment::NordicStoneCrypt;
        s.beaufortScale    = 9.0f;
        s.fireIntensity01  = 0.95f;
        s.fireDistanceU    = 90.0f;
        s.waterIntensity01 = 0.70f;
        s.masterGain       = 0.50f;
        s.muted            = false;
        out.UpdateSceneState(s);

        constexpr size_t kSeg = 14400u; // 300 ms
        std::vector<float> wave(kSeg * 3u * 2u, 0.0f);
        out.RenderOfflineFrames(wave.data(), kSeg);

        // Mute cleanly
        s.muted = true;
        out.UpdateSceneState(s);
        out.RenderOfflineFrames(wave.data() + kSeg * 2u, kSeg);

        // Unmute cleanly
        s.muted = false;
        out.UpdateSceneState(s);
        out.RenderOfflineFrames(wave.data() + kSeg * 2u * 2u, kSeg);

        const WaveMetrics wmAll  = AnalyzeStereoWave(wave, 0u, kSeg * 3u);
        const WaveMetrics wmLoud = AnalyzeStereoWave(wave, kSeg / 2u, kSeg);
        // Last 60 ms of the 300 ms muted window (after >3*tau = 240 ms exponential decay)
        const WaveMetrics wmMuteTail = AnalyzeStereoWave(wave, kSeg * 2u - 2880u, kSeg * 2u);
        const WaveMetrics wmUnmuted  = AnalyzeStereoWave(wave, kSeg * 3u - 4800u, kSeg * 3u);

        if (!checkFn) {
            std::printf("[MuteRamp] loudRms=%.4f muteTailRms=%.5f unmutedRms=%.4f maxJump=%.4f\n",
                        double(wmLoud.rms), double(wmMuteTail.rms), double(wmUnmuted.rms), double(wmAll.maxSampleJump));
        }

        check(wmAll.allFinite && wmAll.maxSampleJump < 0.25f,
              "audio_output_mute_unmute_ramp_zero_clicks");
        check(wmLoud.rms > 0.008f && wmMuteTail.rms < wmLoud.rms * 0.12f && wmUnmuted.rms > 0.008f,
              "audio_output_mute_attenuates_and_unmute_recovers");
    }

    // 4. Live NullSink background worker streaming + lock-free SPSC ring capture
    {
        RawAudioOutput out;
        AudioSceneState s{};
        s.environment      = AcousticEnvironment::OpenWindyTundra;
        s.beaufortScale    = 7.0f;
        s.fireIntensity01  = 0.65f;
        s.fireDistanceU    = 140.0f;
        s.masterGain       = 0.40f;
        out.UpdateSceneState(s);

        const bool started = out.Start(AudioBackendMode::NullSink, 48000u, 202645u);
        std::vector<float> captured;
        captured.reserve(4800u * 2u);
        std::vector<float> chunk(1024u * 2u, 0.0f);

        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(400);
        bool switchedEnv = false;
        while (captured.size() < 2400u * 2u && std::chrono::steady_clock::now() < deadline) {
            const size_t got = out.ReadNullSinkStereo(chunk.data(), 1024u);
            if (got > 0u) {
                captured.insert(captured.end(), chunk.begin(), chunk.begin() + static_cast<ptrdiff_t>(got * 2u));
                if (!switchedEnv && captured.size() >= 960u * 2u) {
                    s.environment = AcousticEnvironment::GlacierIceCavern;
                    out.UpdateSceneState(s);
                    switchedEnv = true;
                }
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
        }
        out.Stop();

        const size_t capFrames = captured.size() / 2u;
        const WaveMetrics wm = AnalyzeStereoWave(captured, 0u, capFrames);
        check(started && capFrames >= 1440u && wm.allFinite && wm.peakAbs <= 1.0f && wm.maxSampleJump < 0.25f,
              "audio_output_nullsink_live_thread_stream_and_ring_capture");
    }

    // 5. Rapid 5x Start/Stop thread-lifecycle stress (zero hangs, fast join < 50 ms each)
    {
        RawAudioOutput out;
        double maxStopMs = 0.0;
        bool allClean = true;
        for (int i = 0; i < 5; ++i) {
            AudioSceneState s{};
            s.environment = static_cast<AcousticEnvironment>(i % static_cast<int>(AcousticEnvironment::Count));
            s.masterGain  = 0.30f;
            out.UpdateSceneState(s);
            if (!out.Start(AudioBackendMode::NullSink, 48000u, 202645u + uint32_t(i))) {
                allClean = false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(6));
            const auto t0 = std::chrono::steady_clock::now();
            out.Stop();
            const double stopMs = std::chrono::duration<double, std::milli>(
                                      std::chrono::steady_clock::now() - t0).count();
            maxStopMs = std::max(maxStopMs, stopMs);
            if (out.IsRunning() || stopMs > 50.0) {
                allClean = false;
            }
        }
        if (!checkFn) {
            std::printf("[Lifecycle 5x] maxStopJoinMs=%.3f ms\n", maxStopMs);
        }
        check(allClean, "audio_output_rapid_5x_start_stop_under_50ms_zero_hang");
    }

    // 6. Graceful WASAPI default endpoint enumeration probe (safe with or without audio hardware)
    {
        const WasapiDeviceInfo info = ProbeWasapiDefaultDevice();
        const bool probeValid = !info.available ||
                                (info.mixSampleRate >= 8000u && info.mixSampleRate <= 384000u &&
                                 info.mixChannels >= 1u && info.bitsPerSample >= 16u);
        if (!checkFn) {
            std::printf("[WASAPI Probe] available=%s rate=%u ch=%u bits=%u float=%s hr=0x%08X\n",
                        info.available ? "YES" : "NO",
                        info.mixSampleRate, info.mixChannels, info.bitsPerSample,
                        info.isFloat ? "YES" : "NO",
                        static_cast<unsigned>(info.lastHresult));
        }
        check(probeValid, "audio_output_wasapi_device_probe_graceful_and_valid");
    }

    // 7. Wave 46 [Audio] INI defaults (LiveAudioAmbience=0, LiveAudioGain=0.22), parsing & safe clamping
    {
        const raw::rt::LiveEngineConfig def = raw::rt::DefaultLiveEngineConfig();
        const std::string iniText = raw::rt::FormatDefaultLiveConfigIni(def);
        raw::rt::LiveEngineConfig parsedDef{};
        raw::rt::ParseLiveConfigIni(iniText, parsedDef);
        const bool defaultOptInOff =
            (!def.liveAudioAmbience) &&
            (std::fabs(def.liveAudioGain - 0.22f) < 1.0e-5f) &&
            (iniText.find("[Audio]") != std::string::npos) &&
            (iniText.find("LiveAudioAmbience=0") != std::string::npos) &&
            (iniText.find("LiveAudioGain=0.22") != std::string::npos) &&
            (!parsedDef.liveAudioAmbience) &&
            (std::fabs(parsedDef.liveAudioGain - 0.22f) < 1.0e-5f);
        check(defaultOptInOff, "audio_bridge_ini_default_opt_in_off_and_gain_022");

        raw::rt::LiveEngineConfig parsedOn{};
        raw::rt::ParseLiveConfigIni("[Audio]\nLiveAudioAmbience=1\nLiveAudioGain=3.50\n", parsedOn);
        const bool parsedClampedOk =
            parsedOn.liveAudioAmbience &&
            std::fabs(parsedOn.liveAudioGain - 0.50f) < 1.0e-5f;
        check(parsedClampedOk, "audio_bridge_ini_parse_on_and_clamp_max_050");
    }

    // 8. Wave 46 rt_audio_bridge enum mapping & runtime mailbox coupling (hearth, water, interior, menu pause)
    {
        const bool enumMapOk =
            raw::rt::MapRuntimeAcousticEnvToDsp(0u) == AcousticEnvironment::OpenWindyTundra &&
            raw::rt::MapRuntimeAcousticEnvToDsp(1u) == AcousticEnvironment::SnowDampedPineForest &&
            raw::rt::MapRuntimeAcousticEnvToDsp(2u) == AcousticEnvironment::NordicStoneCrypt &&
            raw::rt::MapRuntimeAcousticEnvToDsp(3u) == AcousticEnvironment::GlacierIceCavern &&
            raw::rt::MapRuntimeAcousticEnvToDsp(4u) == AcousticEnvironment::Underwater;
        check(enumMapOk, "audio_bridge_5_environment_enum_mapping_exact");

        raw::rt::AudioBridgeInput in{};
        in.acousticEnvClass  = 2u; // NordicStoneCrypt
        in.beaufortScale     = 10.0f;
        in.rain01            = 0.8f;
        in.hearthProximity01 = 0.9f;
        in.inInterior        = true;
        in.inWater           = false;
        in.menuPaused        = false;
        in.liveAudioEnabled  = true;
        in.liveAudioGain     = 0.30f;

        const AudioSceneState cryptState = raw::rt::BuildAudioSceneStateFromRuntime(in);
        const bool cryptOk =
            cryptState.environment == AcousticEnvironment::NordicStoneCrypt &&
            cryptState.beaufortScale < 3.0f && // interior wind muffled
            cryptState.fireIntensity01 > 0.85f &&
            cryptState.fireDistanceU < 130.0f &&
            !cryptState.muted &&
            std::fabs(cryptState.masterGain - 0.30f) < 1.0e-5f;

        in.menuPaused = true;
        const AudioSceneState pausedState = raw::rt::BuildAudioSceneStateFromRuntime(in);

        in.menuPaused = false;
        in.inWater    = true;
        const AudioSceneState waterState = raw::rt::BuildAudioSceneStateFromRuntime(in);

        check(cryptOk && pausedState.muted &&
              waterState.environment == AcousticEnvironment::Underwater &&
              waterState.underwater01 >= 0.99f && !waterState.muted,
              "audio_bridge_mailbox_coupling_hearth_water_interior_pause");
    }

    // 9. Wave 46 AudioBridgeController live hot-toggle 0 -> 1 -> 0 using NullSink (< 5 ms shutdown join)
    {
        raw::rt::AudioBridgeController ctrl;
        ctrl.Init(AudioBackendMode::NullSink, 48000u, 202646u);

        raw::rt::AudioBridgeInput in{};
        in.acousticEnvClass  = 1u; // SnowDampedPineForest
        in.beaufortScale     = 7.0f;
        in.hearthProximity01 = 0.75f;
        in.liveAudioEnabled  = false; // Starts OFF (opt-in)
        in.liveAudioGain     = 0.28f;

        // Tick while OFF: must NOT start worker thread or render frames
        ctrl.Tick(in);
        const raw::rt::AudioBridgeTelemetry tOff0 = ctrl.GetTelemetry();

        // Hot-toggle 0 -> 1
        in.liveAudioEnabled = true;
        ctrl.Tick(in);

        std::vector<float> captured;
        std::vector<float> tmp(1024u * 2u, 0.0f);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(300);
        while (captured.size() < 960u * 2u && std::chrono::steady_clock::now() < deadline) {
            // Also exercise live pause -> unpause + environment shift while streaming!
            in.acousticEnvClass = (captured.size() >= 480u * 2u) ? 3u : 1u; // Forest -> GlacierIceCavern
            ctrl.Tick(in);
            const size_t got = ctrl.Output().ReadNullSinkStereo(tmp.data(), 1024u);
            if (got > 0u) {
                captured.insert(captured.end(), tmp.begin(), tmp.begin() + static_cast<ptrdiff_t>(got * 2u));
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
        }
        const raw::rt::AudioBridgeTelemetry tOn = ctrl.GetTelemetry();

        // Hot-toggle 1 -> 0 and measure clean join time
        in.liveAudioEnabled = false;
        const auto tStop0 = std::chrono::steady_clock::now();
        ctrl.Tick(in);
        const double toggleStopMs = std::chrono::duration<double, std::milli>(
                                        std::chrono::steady_clock::now() - tStop0).count();
        const raw::rt::AudioBridgeTelemetry tOff1 = ctrl.GetTelemetry();
        ctrl.Shutdown();

        const WaveMetrics wm = AnalyzeStereoWave(captured, 0u, captured.size() / 2u);
        if (!checkFn) {
            std::printf("[AudioBridge 0->1->0] frames=%llu captured=%zu stopMs=%.3f rms=%.4f maxJump=%.4f\n",
                        static_cast<unsigned long long>(tOn.framesRendered),
                        captured.size() / 2u, toggleStopMs, double(wm.rms), double(wm.maxSampleJump));
        }

        const bool hotToggleOk =
            !tOff0.streamRunning && tOff0.framesRendered == 0ull && tOff0.toggleStarts == 0u &&
            tOn.enabled && tOn.streamRunning && tOn.toggleStarts == 1u &&
            captured.size() >= 960u * 2u && wm.allFinite && wm.peakAbs <= 1.0f && wm.maxSampleJump < 0.25f &&
            !tOff1.enabled && !tOff1.streamRunning && tOff1.toggleStops == 1u &&
            toggleStopMs < 50.0;
        check(hotToggleOk, "audio_bridge_hot_toggle_0_1_0_nullsink_clean_join");
    }

    if (!checkFn) {
        std::printf("AudioOutputTests completed: %s (failures=%d)\n",
                    failures == 0 ? "PASS" : "FAIL", failures);
    }
    return failures;
}

}  // namespace raw::audio

#ifdef RAW_AUDIO_OUTPUT_STANDALONE_MAIN
int main() {
    const int failures = raw::audio::RunAudioOutputTests(nullptr);
    return failures == 0 ? 0 : 1;
}
#endif
