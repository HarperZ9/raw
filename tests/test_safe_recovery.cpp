// test_safe_recovery.cpp: Wave 62 Verification Suite for Atomic Crash-Loop Safe-Rollover Recovery
// Guard (`rt_safe_recovery`) and `recovery [status|reset]` (13th) Console Dispatcher command.
// Exposes BOTH `int run_safe_recovery_tests()` and `int raw::rt::RunSafeRecoveryTests(checkFn)`.
// Original work, MIT.

#include "rt_safe_recovery.h"
#include "rt_console_diag.h"
#include "rt_live_config.h"
#include "rt_profile_presets.h"

#include <cstdio>
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

int RunSafeRecoveryTests(void (*checkFn)(bool, const char*) = nullptr) {
    int localFail = 0;
    auto Check = [&](bool cond, const char* msg) {
        if (checkFn) {
            checkFn(cond, msg);
        } else {
            std::printf("  [%s] %s\n", cond ? "PASS" : "FAIL", msg);
        }
        if (!cond) ++localFail;
    };

    std::error_code ec;
    const std::filesystem::path tmpDir =
        std::filesystem::temp_directory_path(ec) / "raw_safe_recovery_w62";
    std::filesystem::remove_all(tmpDir, ec);
    std::filesystem::create_directories(tmpDir, ec);

    const std::filesystem::path sentPath = tmpDir / "boot_sentinel.state";
    const std::filesystem::path repPath  = tmpDir / "recovery_report.txt";
    const LiveEngineConfig savedCfg      = GetLiveConfig();

    // -------------------------------------------------------------------------
    // Check 1: Clean Startup Arms Sentinel & Clean Shutdown Clears It
    // -------------------------------------------------------------------------
    {
        (void)ProfilePresetsInit(QualityPreset::Balanced);
        const std::string init1 = SafeRecoveryInit(tmpDir.string());
        const bool armedAfterInit = std::filesystem::exists(sentPath, ec);

        RawSafeRecoveryState st1{};
        const int rcNull = RAW_GetSafeRecoveryState(nullptr);
        const int rc1    = RAW_GetSafeRecoveryState(&st1);

        SafeRecoveryShutdown();
        const bool clearedAfterStop = !std::filesystem::exists(sentPath, ec);

        std::printf("[SafeRecovery] Check1 CleanCycle: init='%s' armed=%u cleared=%u unclean=%u\n",
                    init1.c_str(), armedAfterInit ? 1u : 0u, clearedAfterStop ? 1u : 0u, st1.uncleanBootCount);

        Check(rcNull == 0 && rc1 == 1 &&
              armedAfterInit && clearedAfterStop &&
              st1.guardActive == 1u &&
              st1.uncleanBootCount == 0u &&
              st1.safeRolloverActive == 0u &&
              st1.experimentalDisabled == 0u,
              "SafeRecovery clean boot/shutdown: arms boot_sentinel.state & clears cleanly on shutdown");
    }

    // -------------------------------------------------------------------------
    // Check 2: 2 Consecutive Crashes Trigger Automatic SafeMode Rollover & recovery_report.txt
    // -------------------------------------------------------------------------
    {
        // Boot 1: starts in Ultra, then simulates crash (NO SafeRecoveryShutdown!)
        (void)ProfilePresetsInit(QualityPreset::Ultra);
        SetLiveConfigSnapshot(ApplyPresetToLiveConfig(QualityPreset::Ultra, DefaultLiveEngineConfig()));
        (void)SafeRecoveryInit(tmpDir.string());

        // Boot 2: detects 1st unclean boot (1 < 2 -> no rollover yet), simulates 2nd crash!
        (void)SafeRecoveryInit(tmpDir.string());
        RawSafeRecoveryState stBoot2{};
        RAW_GetSafeRecoveryState(&stBoot2);

        // Boot 3: detects 2nd consecutive unclean boot (2 >= 2 -> AUTOMATIC SAFEMODE ROLLOVER!)
        const std::string boot3Msg = SafeRecoveryInit(tmpDir.string());
        RawSafeRecoveryState stBoot3{};
        RAW_GetSafeRecoveryState(&stBoot3);

        RawProfilePresetState ppAfter{};
        RAW_GetProfilePresetState(&ppAfter);
        const LiveEngineConfig cfgAfter = GetLiveConfig();
        const std::string repTxt = ReadFileAll(repPath);

        std::printf("[SafeRecovery] Check2 CrashLoop: boot2=%u(ro=%u) boot3=%u(ro=%u expDis=%u preset=%s) repBytes=%zu msg='%s'\n",
                    stBoot2.uncleanBootCount, static_cast<unsigned>(stBoot2.safeRolloverActive),
                    stBoot3.uncleanBootCount, static_cast<unsigned>(stBoot3.safeRolloverActive),
                    static_cast<unsigned>(stBoot3.experimentalDisabled),
                    QualityPresetName(static_cast<QualityPreset>(ppAfter.activePreset)),
                    repTxt.size(), boot3Msg.c_str());

        Check(stBoot2.uncleanBootCount == 1u && stBoot2.safeRolloverActive == 0u &&
              stBoot3.uncleanBootCount == 2u &&
              stBoot3.safeRolloverActive == 1u &&
              stBoot3.experimentalDisabled == 1u &&
              ppAfter.activePreset == static_cast<uint8_t>(QualityPreset::SafeMode) &&
              !cfgAfter.dynamicGovernorEnabled &&
              !cfgAfter.skeletonXpbdEnabled &&
              cfgAfter.crashForensicsEnabled &&
              repTxt.find("SAFEMODE ROLLOVER REPORT") != std::string::npos &&
              repTxt.find("SafeMode") != std::string::npos,
              "SafeRecovery 2-crash rollover: forces SafeMode, disables risky overrides & writes recovery_report.txt");
    }

    // -------------------------------------------------------------------------
    // Check 3: 13th Console Command `recovery` / `recovery reset` + Planted Invalid Arg + Cleanup
    // -------------------------------------------------------------------------
    {
        std::string respStatus, respBad, respReset, respHelp;
        const bool okStatus = DispatchDiagnosticCommand("recovery", respStatus);
        const bool okBad    = DispatchDiagnosticCommand("recovery bogus_sub", respBad);
        const bool okReset  = DispatchDiagnosticCommand("recovery reset", respReset);
        const bool okHelp   = DispatchDiagnosticCommand("help", respHelp);

        RawSafeRecoveryState stAfterReset{};
        const int rcReset = RAW_GetSafeRecoveryState(&stAfterReset);

        // Clean shutdown removes sentinel; next boot starts cleanly at uncleanBootCount == 0
        SafeRecoveryShutdown();
        const bool sentGone = !std::filesystem::exists(sentPath, ec);

        (void)ProfilePresetsInit(QualityPreset::Balanced);
        (void)SafeRecoveryInit(tmpDir.string());
        RawSafeRecoveryState stFresh{};
        RAW_GetSafeRecoveryState(&stFresh);

        SafeRecoveryShutdown();
        ProfilePresetsShutdown();
        SetLiveConfigSnapshot(savedCfg);

        std::filesystem::remove_all(tmpDir, ec);
        const bool cleaned = !std::filesystem::exists(tmpDir, ec);

        std::printf("[SafeRecovery] Check3 Console+Reset: status='%s' reset='%s' freshUnclean=%u clean=%u\n",
                    respStatus.c_str(), respReset.c_str(), stFresh.uncleanBootCount, cleaned ? 1u : 0u);

        Check(okStatus && !okBad && okReset && okHelp && rcReset == 1 &&
              respStatus.find("safeRollover=1") != std::string::npos &&
              respBad.find("recovery [status|reset]") != std::string::npos &&
              respReset.find("counter reset") != std::string::npos &&
              respHelp.find("recovery") != std::string::npos &&
              stAfterReset.uncleanBootCount == 0u &&
              stAfterReset.safeRolloverActive == 0u &&
              stAfterReset.manualResets >= 1u &&
              sentGone &&
              stFresh.uncleanBootCount == 0u &&
              stFresh.safeRolloverActive == 0u &&
              cleaned,
              "SafeRecovery `recovery [status|reset]` 13th command, manual reset & clean boot restoration verified");
    }

    return localFail;
}

}  // namespace raw::rt

int run_safe_recovery_tests() {
    return raw::rt::RunSafeRecoveryTests(nullptr);
}

#ifdef RAW_SAFE_RECOVERY_STANDALONE_MAIN
int main() {
    std::printf("=== RAW Wave 62: Atomic Crash-Loop Safe-Rollover Recovery Guard Suite ===\n");
    const int fail = run_safe_recovery_tests();
    std::printf("=== RESULT: %s (failures=%d) ===\n", fail == 0 ? "ALL PASS" : "FAIL", fail);
    return fail == 0 ? 0 : 1;
}
#endif
