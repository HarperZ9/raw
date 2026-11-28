// test_telemetry_recorder.cpp: Wave 52 Zero-Allocation 300-Frame Engine Flight-Recorder Ring &
// Profiler JSON Dump Verification Suite.
// Linkable into ce_runtime_check via raw::rt::RunTelemetryRecorderTests(checkFn),
// or buildable standalone with -DRAW_TELEMETRY_RECORDER_STANDALONE_MAIN.
// Original work, MIT.

#include "../src/engine_rt/rt_live_config.h"
#include "../src/engine_rt/rt_telemetry_recorder.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace raw::rt {

int RunTelemetryRecorderTests(void (*checkFn)(bool, const char*)) {
    int failures = 0;
    auto check = [&](bool ok, const char* name) {
        if (!ok) ++failures;
        if (checkFn) {
            checkFn(ok, name);
        } else {
            std::printf("%s %s\n", ok ? "PASS" : "FAIL", name);
        }
    };

    // 1. Ring wrap-around proof: recording 450 frames retains exact newest 300 frames (150..449) chronologically!
    {
        FlightRecorderRing ring;
        for (uint64_t i = 0ull; i < 450ull; ++i) {
            FlightFrameSample s{};
            s.frameIndex        = i;
            s.timestampSec      = double(i) * (1.0 / 60.0);
            s.frameMs           = 16.0f + float(i % 10ull) * 0.1f;
            s.surfaceTempC      = -5.0f;
            s.snowpackM         = 0.42f;
            s.windBeaufort      = 4.0f;
            s.audioMood         = 2u;
            s.shelterPressure01 = 0.65f;
            s.activeHandles     = 12345u;
            ring.RecordFrame(s);
        }

        std::array<FlightFrameSample, FlightRecorderRing::kCapacity> out{};
        const size_t n = ring.CopyChronological(out.data(), out.size());
        const FlightRecorderStats st = ring.ComputeStats();

        bool orderOk = (n == 300u && st.totalRecordedFrames == 450ull && st.ringFillCount == 300u);
        for (size_t i = 0u; i < n && orderOk; ++i) {
            if (out[i].frameIndex != 150ull + uint64_t(i)) orderOk = false;
        }
        check(orderOk, "flight_recorder_450_frames_wraps_cleanly_retaining_150_to_449");
    }

    // 2. Exact percentile accuracy proof (1..100 ms uniform distribution -> p50~50.5, p95~95.5, p99~99, max=100, hitches=50)
    {
        FlightRecorderRing ring;
        // Feed 100 frames with frameMs = 1.0, 2.0, ..., 100.0 ms (shuffled order to prove sorting!)
        for (uint32_t i = 0u; i < 100u; ++i) {
            const uint32_t val = ((i * 37u) % 100u) + 1u; // bijection over 1..100
            FlightFrameSample s{};
            s.frameIndex   = i;
            s.timestampSec = double(i) * 0.016;
            s.frameMs      = float(val);
            ring.RecordFrame(s);
        }

        const FlightRecorderStats st = ring.ComputeStats();
        // Exact analytical values for {1, 2, ..., 100}:
        // mean = 50.5, p50 in [50..51], p95 in [95..96], p99 in [99..100], max = 100.0, hitches (>50ms) = 50 (51..100)!
        if (!checkFn) {
            std::printf("[FlightStats 1..100] mean=%.2f p50=%.1f p95=%.1f p99=%.1f max=%.1f hitches=%u\n",
                        double(st.meanFrameMs), double(st.p50FrameMs), double(st.p95FrameMs),
                        double(st.p99FrameMs), double(st.maxFrameMs), st.hitchFramesOver50ms);
        }
        const bool statsOk =
            std::fabs(st.meanFrameMs - 50.5f) < 0.05f &&
            st.p50FrameMs >= 50.0f && st.p50FrameMs <= 51.0f &&
            st.p95FrameMs >= 95.0f && st.p95FrameMs <= 96.0f &&
            st.p99FrameMs >= 99.0f && st.p99FrameMs <= 100.0f &&
            std::fabs(st.maxFrameMs - 100.0f) < 0.01f &&
            st.hitchFramesOver50ms == 50u;
        check(statsOk, "flight_recorder_exact_p50_p95_p99_max_and_hitch_count");
    }

    // 3. JSON serialization & atomic disk flush proof (writes temp JSON, validates schema/order, cleans up temp file!)
    {
        FlightRecorderRing ring;
        for (uint64_t i = 0ull; i < 310ull; ++i) {
            FlightFrameSample s{};
            s.frameIndex        = i;
            s.timestampSec      = double(i) * 0.0166;
            s.frameMs           = (i == 305ull) ? 72.5f : 16.6f;
            s.surfaceTempC      = -12.5f;
            s.snowpackM         = 0.75f;
            s.windBeaufort      = 6.0f;
            s.audioMood         = 2u;
            s.shelterPressure01 = 0.80f;
            s.hitchCount        = (i >= 305ull) ? 1u : 0u;
            s.activeHandles     = 54321u;
            s.modAuditSeverity  = 0u;
            ring.RecordFrame(s);
        }

        const std::filesystem::path tmpFile =
            std::filesystem::temp_directory_path() / "raw_test_flight_recorder_w52.json";
        const std::string tmpPathStr = tmpFile.string();
        std::error_code ec;
        std::filesystem::remove(tmpFile, ec);

        const bool flushed = ring.FlushJsonToFile(tmpPathStr.c_str());
        std::string diskJson;
        if (flushed) {
            std::ifstream ifs(tmpFile, std::ios::binary);
            std::ostringstream ss;
            ss << ifs.rdbuf();
            diskJson = ss.str();
        }
        std::filesystem::remove(tmpFile, ec);

        const size_t posFirst = diskJson.find("\"frameIndex\":10,");
        const size_t posLast  = diskJson.find("\"frameIndex\":309,");
        const bool jsonOk =
            flushed &&
            ring.FlushCount() == 1u &&
            diskJson.size() > 4096u &&
            diskJson.front() == '{' &&
            diskJson.find("\"schema\": \"raw_flight_recorder_v1\"") != std::string::npos &&
            diskJson.find("\"totalRecordedFrames\": 310") != std::string::npos &&
            diskJson.find("\"ringFillCount\": 300") != std::string::npos &&
            diskJson.find("\"hitchFramesOver50ms\": 1") != std::string::npos &&
            diskJson.find("\"frameIndex\":9,") == std::string::npos && // frame 9 evicted!
            posFirst != std::string::npos &&
            posLast != std::string::npos &&
            posFirst < posLast; // strict chronological order!
        check(jsonOk, "flight_recorder_json_flush_valid_chronological_and_cleaned");
    }

    // 4. RAW_GetFlightRecorderStats C ABI + [Diagnostics] FlightRecorder INI round-trip & 0 disable gate!
    {
        const LiveEngineConfig prevCfg = GetLiveConfig();
        LiveEngineConfig onCfg = DefaultLiveEngineConfig();
        onCfg.flightRecorderEnabled = true;
        SetLiveConfigSnapshot(onCfg);

        FlightRecorderInit("");
        for (int i = 0; i < 20; ++i) {
            FlightRecorderOnPresent(16.6);
        }
        FlightRecorderOnPresent(64.0);

        RawFlightRecorderStats stOn{};
        const int okOn = RAW_GetFlightRecorderStats(&stOn);
        const bool cAbiOnOk =
            okOn == 1 &&
            stOn.recorderEnabled == 1u &&
            stOn.totalRecordedFrames == 21ull &&
            stOn.ringFillCount == 21u &&
            stOn.hitchFramesOver50ms == 1u &&
            std::fabs(stOn.maxFrameMs - 64.0f) < 0.05f;
        check(cAbiOnOk, "flight_recorder_c_abi_exports_live_stats");

        const std::string defIni = FormatDefaultLiveConfigIni(DefaultLiveEngineConfig());
        LiveEngineConfig parsedDef{};
        ParseLiveConfigIni(defIni, parsedDef);
        LiveEngineConfig parsedOff{};
        ParseLiveConfigIni("[Diagnostics]\nFlightRecorder=0\n", parsedOff);

        SetLiveConfigSnapshot(parsedOff);
        FlightRecorderOnPresent(150.0); // MUST be ignored while FlightRecorder=0!

        RawFlightRecorderStats stOff{};
        RAW_GetFlightRecorderStats(&stOff);
        const bool iniGateOk =
            defIni.find("FlightRecorder=1") != std::string::npos &&
            parsedDef.flightRecorderEnabled &&
            !parsedOff.flightRecorderEnabled &&
            stOff.recorderEnabled == 0u &&
            stOff.totalRecordedFrames == 21ull && // 150ms spike ignored!
            stOff.hitchFramesOver50ms == 1u;
        check(iniGateOk, "flight_recorder_diagnostics_ini_roundtrip_and_disable_gate");

        FlightRecorderShutdown();
        SetLiveConfigSnapshot(prevCfg);
    }

    if (!checkFn) {
        std::printf("TelemetryRecorderTests completed: %s (failures=%d)\n",
                    failures == 0 ? "PASS" : "FAIL", failures);
    }
    return failures;
}

}  // namespace raw::rt

#ifdef RAW_TELEMETRY_RECORDER_STANDALONE_MAIN
int main() {
    const int failures = raw::rt::RunTelemetryRecorderTests(nullptr);
    return failures == 0 ? 0 : 1;
}
#endif
