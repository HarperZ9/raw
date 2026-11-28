// test_live_dashboard.cpp: Wave 59 Verification Suite for Non-Blocking Live JSON/HTML Telemetry
// Snapshot Dashboard (`rt_live_dashboard`) and `dashboard` Console Dispatcher command.
// Exposes BOTH `int run_live_dashboard_tests()` and `int raw::rt::RunLiveDashboardTests(checkFn)`.
// Original work, MIT.

#include "rt_live_dashboard.h"
#include "rt_adaptive_audio_director.h"
#include "rt_console_diag.h"
#include "rt_engine_health.h"
#include "rt_frame_governor.h"
#include "rt_profile_presets.h"
#include "rt_shader_hotreload.h"
#include "rt_telemetry_recorder.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace raw::rt {

namespace {

std::string ReadFileAll(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return {};
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

}  // namespace

int RunLiveDashboardTests(void (*checkFn)(bool, const char*) = nullptr) {
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
    // Check 1: Pure JSON & Self-Contained HTML Formatter Integrity (Zero CDN, Zero Em-Dash, Escaping)
    // -------------------------------------------------------------------------
    {
        LiveDashboardSnapshot snap{};
        snap.exportNumber        = 7u;
        snap.sdkVersion          = 0x00010000u;
        snap.activeSubsystemMask = 0x0001FFFFu;
        snap.verifiedGmstModules = 8u;
        snap.healthScore         = 92u;
        snap.healthGrade         = 0u;
        snap.advisoryCount       = 1u;
        std::snprintf(snap.advisories[0], sizeof(snap.advisories[0]),
                      "TEST_ADV: <check \"quotes\" & slash\\ok>");
        snap.p95FrameMs          = 14.25f;
        snap.renderScale         = 0.95f;
        snap.shaderReloadGen     = 3u;
        std::snprintf(snap.lastDirtyShader, sizeof(snap.lastDirtyShader), "RtWater.hlsl");

        const std::string json = FormatLiveDashboardJson(snap);
        const std::string html = FormatLiveDashboardHtml(snap);

        // UTF-8 em dash sequence: 0xE2 0x80 0x94
        const std::string emDash("\xE2\x80\x94", 3u);
        const bool noEmDash = (json.find(emDash) == std::string::npos) &&
                              (html.find(emDash) == std::string::npos);
        const bool noCdn    = (html.find("http://") == std::string::npos) &&
                              (html.find("https://") == std::string::npos) &&
                              (html.find("<script") == std::string::npos);

        const bool jsonBlocksOk =
            !json.empty() && json.front() == '{' &&
            json.find("\"dashboardVersion\"") != std::string::npos &&
            json.find("\"sdk\"") != std::string::npos &&
            json.find("\"health\"") != std::string::npos &&
            json.find("\"framePacing\"") != std::string::npos &&
            json.find("\"governor\"") != std::string::npos &&
            json.find("\"presets\"") != std::string::npos &&
            json.find("\"shaderHotReload\"") != std::string::npos &&
            json.find("\"audio\"") != std::string::npos &&
            json.find("\"worldAndMods\"") != std::string::npos &&
            json.find("\\\"quotes\\\"") != std::string::npos &&
            json.find("slash\\\\ok") != std::string::npos;

        const bool htmlBlocksOk =
            html.find("<!DOCTYPE html>") == 0u &&
            html.find("</html>") != std::string::npos &&
            html.find("Engine Health Scoreboard") != std::string::npos &&
            html.find("PI Quality Governor") != std::string::npos &&
            html.find("VRAM Watchdog") != std::string::npos &&
            html.find("Shader Hot-Reload Watchdog") != std::string::npos &&
            html.find("RtWater.hlsl") != std::string::npos &&
            html.find("&lt;check &quot;quotes&quot; &amp; slash\\ok&gt;") != std::string::npos;

        std::printf("[LiveDashboard] Check1 Formatters: json=%zuB html=%zuB noCdn=%u noEmDash=%u\n",
                    json.size(), html.size(), noCdn ? 1u : 0u, noEmDash ? 1u : 0u);

        Check(noEmDash && noCdn && jsonBlocksOk && htmlBlocksOk,
              "LiveDashboard formatters: valid escaped JSON & self-contained HTML5 (zero CDN, zero em dashes)");
    }

    // -------------------------------------------------------------------------
    // Check 2: 300-Frame Auto-Cadence, Atomic Disk Export, C ABI & Console `dashboard` Command
    // -------------------------------------------------------------------------
    {
        std::error_code ec;
        const std::filesystem::path tmpDir =
            std::filesystem::temp_directory_path(ec) / "raw_live_dashboard_w59";
        std::filesystem::remove_all(tmpDir, ec);
        std::filesystem::create_directories(tmpDir, ec);

        const std::filesystem::path jsonPath = tmpDir / "raw_live_dashboard.json";
        const std::filesystem::path htmlPath = tmpDir / "raw_live_dashboard.html";

        (void)EngineHealthInit();
        (void)FrameGovernorInit();
        (void)ProfilePresetsInit(QualityPreset::Balanced);
        (void)AudioDirectorInit();
        (void)FlightRecorderInit(tmpDir.string());
        const std::string initMsg = LiveDashboardInit(tmpDir.string());

        FlightFrameSample sample{};
        sample.frameIndex = 1ull;
        sample.frameMs    = 13.8f;
        FlightRecorderRecordDirect(sample);

        // Frames 1..299 must NOT auto-export prematurely
        for (int i = 0; i < 299; ++i) {
            LiveDashboardOnPresent();
        }
        const bool noEarlyWrite = !std::filesystem::exists(jsonPath, ec) &&
                                  !std::filesystem::exists(htmlPath, ec);

        // Frame 300 triggers automatic atomic JSON + HTML export
        LiveDashboardOnPresent();
        const bool wroteAt300 = std::filesystem::exists(jsonPath, ec) &&
                                std::filesystem::exists(htmlPath, ec);

        RawLiveDashboardState st1{};
        const int rcNull = RAW_GetLiveDashboardState(nullptr);
        const int rc1    = RAW_GetLiveDashboardState(&st1);

        // On-demand `dashboard` 10th command in rt_console_diag increments exportCount 1 -> 2
        std::string respDash, respHelp;
        const bool okDash = DispatchDiagnosticCommand("dashboard", respDash);
        const bool okHelp = DispatchDiagnosticCommand("help", respHelp);

        RawLiveDashboardState st2{};
        const int rc2 = RAW_GetLiveDashboardState(&st2);

        const std::string diskJson = ReadFileAll(jsonPath);
        const std::string diskHtml = ReadFileAll(htmlPath);

        // Clean shutdown & temp directory removal
        LiveDashboardShutdown();
        FlightRecorderShutdown();
        AudioDirectorShutdown();
        ProfilePresetsShutdown();
        FrameGovernorShutdown();
        EngineHealthShutdown();

        std::filesystem::remove_all(tmpDir, ec);
        const bool cleanedUp = !std::filesystem::exists(tmpDir, ec);

        std::printf("[LiveDashboard] Check2+3 Runtime+Console: init='%s' exports=%u->%u json=%uB html=%uB cmd='%s' clean=%u\n",
                    initMsg.c_str(), st1.exportCount, st2.exportCount,
                    st2.lastJsonBytes, st2.lastHtmlBytes, respDash.c_str(), cleanedUp ? 1u : 0u);

        Check(noEarlyWrite && wroteAt300 &&
              rcNull == 0 && rc1 == 1 && rc2 == 1 &&
              st1.dashboardActive == 1u && st1.lastExportOk == 1u &&
              st1.exportCount == 1u && st2.exportCount == 2u &&
              st2.lastJsonBytes == diskJson.size() && st2.lastJsonBytes > 500u &&
              st2.lastHtmlBytes == diskHtml.size() && st2.lastHtmlBytes > 1500u &&
              okDash && okHelp &&
              respDash.find("exported=yes") != std::string::npos &&
              respHelp.find("dashboard") != std::string::npos &&
              diskJson.find("\"shaderHotReload\"") != std::string::npos &&
              diskHtml.find("<!DOCTYPE html>") == 0u &&
              cleanedUp,
              "LiveDashboard 300-frame cadence, atomic JSON/HTML export, C ABI & `dashboard` console command");
    }

    return localFail;
}

}  // namespace raw::rt

int run_live_dashboard_tests() {
    return raw::rt::RunLiveDashboardTests(nullptr);
}

#ifdef RAW_LIVE_DASHBOARD_STANDALONE_MAIN
int main() {
    std::printf("=== RAW Wave 59: Non-Blocking Live JSON/HTML Telemetry Dashboard Suite ===\n");
    const int fail = run_live_dashboard_tests();
    std::printf("=== RESULT: %s (failures=%d) ===\n", fail == 0 ? "ALL PASS" : "FAIL", fail);
    return fail == 0 ? 0 : 1;
}
#endif
