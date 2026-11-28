// test_console_diag.cpp: Wave 56 Verification Suite for Live File-Triggered Engine Diagnostic
// Command Dispatcher (`rt_console_diag`).
// Compiles both as a standalone binary (RAW_CONSOLE_DIAG_STANDALONE_MAIN) and inside ce_runtime_check.
// Original work, MIT.

#include "rt_console_diag.h"

#include "rt_adaptive_audio_director.h"
#include "rt_engine_health.h"
#include "rt_frame_governor.h"
#include "rt_sdk_bridge.h"
#include "rt_telemetry_recorder.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace raw::rt {

int RunConsoleDiagTests(void (*checkFn)(bool, const char*) = nullptr) {
    int localFail = 0;
    auto Check = [&](bool cond, const char* msg) {
        if (checkFn) {
            checkFn(cond, msg);
        } else {
            std::printf("  [%s] %s\n", cond ? "PASS" : "FAIL", msg);
        }
        if (!cond) ++localFail;
    };

    // -------------------------------------------------------------------------
    // Check 1: Planted-Failure Controls (nullptr, empty/whitespace, bogus command)
    // -------------------------------------------------------------------------
    {
        std::string rNull, rEmpty, rSpace, rBogus, rBadVerb;
        const bool okNull    = DispatchDiagnosticCommand(nullptr, rNull);
        const bool okEmpty   = DispatchDiagnosticCommand("", rEmpty);
        const bool okSpace   = DispatchDiagnosticCommand("   \t\r\n  ", rSpace);
        const bool okBogus   = DispatchDiagnosticCommand("bogus_cmd", rBogus);
        const bool okBadVerb = DispatchDiagnosticCommand("health_invalid", rBadVerb);

        std::printf("[ConsoleDiag] Check1 PlantedFailures: null=%d empty=%d space=%d bogus=%d badVerb=%d\n",
                    okNull ? 1 : 0, okEmpty ? 1 : 0, okSpace ? 1 : 0, okBogus ? 1 : 0, okBadVerb ? 1 : 0);

        Check(!okNull && !okEmpty && !okSpace && !okBogus && !okBadVerb &&
              rNull.find("help") != std::string::npos &&
              rEmpty.find("help") != std::string::npos &&
              rSpace.find("help") != std::string::npos &&
              rBogus.find("bogus_cmd") != std::string::npos &&
              rBogus.find("help") != std::string::npos &&
              rBadVerb.find("help") != std::string::npos,
              "ConsoleDiag planted failures: nullptr/empty/whitespace/bogus_cmd cleanly rejected with usage hint");
    }

    // -------------------------------------------------------------------------
    // Check 2: All 7 Commands (help, health, sdk, flight, mods, saves, status) + Case/Trim Normalization
    // -------------------------------------------------------------------------
    {
        std::error_code ec;
        const std::filesystem::path tmpDir =
            std::filesystem::temp_directory_path(ec) / "raw_console_diag_cmds_w56";
        std::filesystem::remove_all(tmpDir, ec);

        (void)EngineHealthInit();
        (void)FrameGovernorInit();
        (void)AudioDirectorInit();
        (void)FlightRecorderInit(tmpDir.string());
        (void)SdkBridgeInit(tmpDir.string());

        FlightFrameSample s{};
        s.frameIndex = 1ull;
        s.frameMs    = 15.2f;
        FlightRecorderRecordDirect(s);

        std::string rHelp, rHealth, rSdk, rFlight, rMods, rSaves, rStatus, rMixed;
        const bool okHelp   = DispatchDiagnosticCommand("help", rHelp);
        const bool okHealth = DispatchDiagnosticCommand("health", rHealth);
        const bool okSdk    = DispatchDiagnosticCommand("sdk", rSdk);
        const bool okFlight = DispatchDiagnosticCommand("flight", rFlight);
        const bool okMods   = DispatchDiagnosticCommand("mods", rMods);
        const bool okSaves  = DispatchDiagnosticCommand("saves", rSaves);
        const bool okStatus = DispatchDiagnosticCommand("status", rStatus);
        const bool okMixed  = DispatchDiagnosticCommand("  StAtUs \r\n", rMixed);

        std::printf("[ConsoleDiag] Check2 Commands:\n"
                    "  health -> %s\n"
                    "  sdk    -> %s\n"
                    "  flight -> %s\n"
                    "  mods   -> %s\n"
                    "  saves  -> %s\n"
                    "  status -> %s\n",
                    rHealth.c_str(), rSdk.c_str(), rFlight.c_str(),
                    rMods.c_str(), rSaves.c_str(), rStatus.c_str());

        const bool sdkJsonExists    = std::filesystem::exists(tmpDir / "unified_sdk_snapshot.json", ec);
        const bool flightJsonExists = std::filesystem::exists(tmpDir / "session_flight_recorder.json", ec);

        SdkBridgeShutdown();
        FlightRecorderShutdown();
        AudioDirectorShutdown();
        FrameGovernorShutdown();
        EngineHealthShutdown();
        std::filesystem::remove_all(tmpDir, ec);

        Check(okHelp && okHealth && okSdk && okFlight && okMods && okSaves && okStatus && okMixed &&
              rHelp.find("status") != std::string::npos &&
              rHealth.find("[EngineHealth]") != std::string::npos &&
              rSdk.find("0x00010000") != std::string::npos &&
              rSdk.find("jsonFlushed=yes") != std::string::npos &&
              rFlight.find("[Flight]") != std::string::npos &&
              rFlight.find("p95=") != std::string::npos &&
              rFlight.find("jsonFlushed=yes") != std::string::npos &&
              rMods.find("[Mods]") != std::string::npos &&
              rSaves.find("[Saves]") != std::string::npos &&
              rStatus.find("[Status]") != std::string::npos &&
              rMixed.find("[Status]") != std::string::npos &&
              sdkJsonExists && flightJsonExists,
              "ConsoleDiag all 7 commands execute cleanly with verified structured outputs & JSON flushes");
    }

    // -------------------------------------------------------------------------
    // Check 3: End-to-End File-Trigger Poll Round-Trip (raw_cmd.txt -> raw_cmd_result.txt + single-shot delete)
    // -------------------------------------------------------------------------
    {
        std::error_code ec;
        const std::filesystem::path tmpDir =
            std::filesystem::temp_directory_path(ec) / "raw_console_diag_poll_w56";
        std::filesystem::remove_all(tmpDir, ec);
        std::filesystem::create_directories(tmpDir, ec);

        const std::filesystem::path cmdPath = tmpDir / "raw_cmd.txt";
        const std::filesystem::path resPath = tmpDir / "raw_cmd_result.txt";

        (void)EngineHealthInit();
        (void)FrameGovernorInit();
        (void)AudioDirectorInit();
        const std::string initMsg = ConsoleDiagInit(tmpDir.string());

        {
            std::ofstream out(cmdPath, std::ios::binary | std::ios::trunc);
            out << "# diagnostic test batch\nhealth\nstatus\n";
        }

        // Frames 1..59 must NOT poll prematurely
        for (int i = 0; i < 59; ++i) {
            ConsoleDiagOnPresent();
        }
        const bool stillPresentAt59 = std::filesystem::exists(cmdPath, ec);
        const bool noResultAt59     = !std::filesystem::exists(resPath, ec);

        // Frame 60 triggers single-shot read, execute, atomic result write, and delete of raw_cmd.txt
        ConsoleDiagOnPresent();
        const bool cmdConsumedAt60 = !std::filesystem::exists(cmdPath, ec);
        const bool resWrittenAt60  = std::filesystem::exists(resPath, ec);

        std::string resultText;
        if (resWrittenAt60) {
            std::ifstream in(resPath, std::ios::binary);
            std::ostringstream ss;
            ss << in.rdbuf();
            resultText = ss.str();
        }

        const uint32_t batchesAfter60 = ConsoleDiagBatchesProcessed();
        const uint32_t cmdsAfter60    = ConsoleDiagCommandsExecuted();

        // Ticking another 60 frames must NOT re-execute (single-shot consumption!)
        for (int i = 0; i < 60; ++i) {
            ConsoleDiagOnPresent();
        }
        const uint32_t batchesAfter120 = ConsoleDiagBatchesProcessed();

        ConsoleDiagShutdown();
        AudioDirectorShutdown();
        FrameGovernorShutdown();
        EngineHealthShutdown();

        std::filesystem::remove_all(tmpDir, ec);
        const bool cleanedUp = !std::filesystem::exists(tmpDir, ec);

        std::printf("[ConsoleDiag] Check3 FilePoll: init='%s' consumed=%u resBytes=%zu batches=%u->%u cmds=%u cleaned=%u\n",
                    initMsg.c_str(), cmdConsumedAt60 ? 1u : 0u, resultText.size(),
                    batchesAfter60, batchesAfter120, cmdsAfter60, cleanedUp ? 1u : 0u);

        Check(stillPresentAt59 && noResultAt59 &&
              cmdConsumedAt60 && resWrittenAt60 &&
              batchesAfter60 == 1u && batchesAfter120 == 1u && cmdsAfter60 == 2u &&
              resultText.find("[health] (OK)") != std::string::npos &&
              resultText.find("[EngineHealth]") != std::string::npos &&
              resultText.find("[status] (OK)") != std::string::npos &&
              resultText.find("[Status]") != std::string::npos &&
              cleanedUp,
              "ConsoleDiag file-trigger poll: 60-frame cadence consumes raw_cmd.txt once, writes atomic raw_cmd_result.txt & cleans up");
    }

    return localFail;
}

}  // namespace raw::rt

#ifdef RAW_CONSOLE_DIAG_STANDALONE_MAIN
int main() {
    std::printf("=== RAW Wave 56: Live File-Triggered Engine Diagnostic Command Dispatcher Suite ===\n");
    const int fail = raw::rt::RunConsoleDiagTests(nullptr);
    std::printf("=== RESULT: %s (failures=%d) ===\n", fail == 0 ? "ALL PASS" : "FAIL", fail);
    return fail == 0 ? 0 : 1;
}
#endif
