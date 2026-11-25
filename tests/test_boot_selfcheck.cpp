// test_boot_selfcheck.cpp: Wave 60 Verification Suite for 12-Point Live Boot & Runtime
// Self-Diagnostic Verifier (`rt_boot_selfcheck`) and `selfcheck` Console Dispatcher command.
// Exposes BOTH `int run_boot_selfcheck_tests()` and `int raw::rt::RunBootSelfCheckTests(checkFn)`.
// Original work, MIT.

#include "rt_boot_selfcheck.h"
#include "rt_adaptive_audio_director.h"
#include "rt_console_diag.h"
#include "rt_engine_health.h"
#include "rt_frame_governor.h"
#include "rt_live_dashboard.h"
#include "rt_profile_presets.h"
#include "rt_shader_hotreload.h"
#include "rt_telemetry_recorder.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace raw::rt {

namespace {

bool WriteFile(const std::filesystem::path& p, const std::string& s) {
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(s.data(), static_cast<std::streamsize>(s.size()));
    return out.good();
}

std::string ReadFile(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return {};
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

}  // namespace

int RunBootSelfCheckTests(void (*checkFn)(bool, const char*) = nullptr) {
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
    const std::filesystem::path tmpRoot =
        std::filesystem::temp_directory_path(ec) / "raw_boot_selfcheck_w60";
    std::filesystem::remove_all(tmpRoot, ec);
    const std::filesystem::path fakeGame    = tmpRoot / "game";
    const std::filesystem::path fakeVers    = fakeGame / "raw_versions";
    const std::filesystem::path fakeShaders = fakeGame / "raw_shaders";
    const std::filesystem::path fakeOut     = tmpRoot / "out";
    std::filesystem::create_directories(fakeVers, ec);
    std::filesystem::create_directories(fakeShaders, ec);
    std::filesystem::create_directories(fakeOut, ec);

    // Populate clean synthetic TSV (>=60 rows) & RTTI (>=1050 rows) fixtures + valid shader
    {
        std::ostringstream tsv;
        tsv << "# skyrimse-1.7.104.tsv fixture\n";
        for (int i = 0; i < 64; ++i) {
            char line[64];
            std::snprintf(line, sizeof(line), "sym_%03d\t0x%06X\n", i, 0x100000 + i * 16);
            tsv << line;
        }
        WriteFile(fakeVers / "skyrimse-1.7.104.tsv", tsv.str());

        std::ostringstream rtti;
        rtti << "class_name\tvtable_rva\n";
        for (int i = 0; i < 1050; ++i) {
            char line[64];
            std::snprintf(line, sizeof(line), "TESClass_%04d\t0x%06X\n", i, 0x1800000 + i * 8);
            rtti << line;
        }
        WriteFile(fakeVers / "skyrimse-1.7.104-rtti.tsv", rtti.str());

        WriteFile(fakeShaders / "SkyPass.hlsl", "float4 main() : SV_Target { return float4(1,1,1,1); }\n");
    }

    // Initialize live runtime subsystems so all 12 checks evaluate cleanly
    (void)EngineHealthInit();
    (void)FrameGovernorInit();
    (void)ProfilePresetsInit(QualityPreset::Balanced);
    (void)AudioDirectorInit();
    (void)FlightRecorderInit(fakeOut.string());
    (void)ShaderHotReloadInit(fakeShaders.string());
    (void)LiveDashboardInit(fakeOut.string());

    // -------------------------------------------------------------------------
    // Check 1: Clean 12-Point Boot Self-Check + Atomic boot_selfcheck.txt Export + C ABI
    // -------------------------------------------------------------------------
    {
        const std::string initMsg = BootSelfCheckInit(fakeGame.string(), fakeOut.string());
        RawBootSelfCheckState st1{};
        const int rcNull = RAW_GetBootSelfCheckState(nullptr);
        const int rc1    = RAW_GetBootSelfCheckState(&st1);

        const std::filesystem::path repPath = fakeOut / "boot_selfcheck.txt";
        const std::string repTxt = ReadFile(repPath);

        std::printf("[BootSelfCheck] Check1 CleanBoot: init='%s' pass=%u/12 warn=%u fail=%u mask=0x%03X bytes=%zu\n",
                    initMsg.c_str(), st1.passCount, st1.warnCount, st1.failCount,
                    st1.passBitmask, repTxt.size());

        Check(rcNull == 0 && rc1 == 1 &&
              st1.initialized == 1u &&
              st1.totalChecks == kBootSelfCheckCount &&
              st1.passCount == kBootSelfCheckCount &&
              st1.warnCount == 0u &&
              st1.failCount == 0u &&
              st1.passBitmask == 0x0FFFu &&
              st1.failBitmask == 0u &&
              st1.tsvRowCount == 64u &&
              st1.rttiRowCount == 1050u &&
              st1.lastReportWritten == 1u &&
              repTxt.find("PeBinaryChecksum") != std::string::npos &&
              repTxt.find("FrameGovernorStability") != std::string::npos,
              "BootSelfCheck clean 12/12 boot: all 12 points PASS (mask=0xFFF), C ABI & boot_selfcheck.txt valid");
    }

    // -------------------------------------------------------------------------
    // Check 2: Planted-Failure Controls (Corrupt PE, Missing TSV/RTTI, Bad Shader Edit)
    // -------------------------------------------------------------------------
    {
        const std::filesystem::path badExe = tmpRoot / "corrupt_skyrim.exe";
        WriteFile(badExe, "NOT_A_VALID_PE64_HEADER_BYTES");

        // Trigger a fail-closed syntax rejection in rt_shader_hotreload
        WriteFile(fakeShaders / "SkyPass.hlsl", "float4 main() : SV_Target { return float4(1,1,1,1);\n");
        (void)ShaderHotReloadPollScan(false);

        BootSelfCheckOptions badOpts{};
        badOpts.gameDir          = fakeGame.string();
        badOpts.outDir           = fakeOut.string();
        badOpts.hostExePath      = badExe.string();
        badOpts.tsvPathOverride  = (tmpRoot / "missing_offsets.tsv").string();
        badOpts.rttiPathOverride = (tmpRoot / "missing_rtti.tsv").string();

        const BootSelfCheckReport badRep = RunBootSelfCheck(badOpts);
        const std::string badFormatted   = FormatBootSelfCheckReport(badRep);

        std::printf("[BootSelfCheck] Check2 PlantedFailures: pass=%u fail=%u failMask=0x%03X\n",
                    badRep.passCount, badRep.failCount, badRep.failBitmask);

        // Bits 0 (PE), 1 (TSV), 2 (RTTI), 3 (ShaderHotReload) MUST all fail closed!
        Check(badRep.failCount >= 4u &&
              (badRep.failBitmask & 0x00Fu) == 0x00Fu &&
              badFormatted.find("Overall: FAIL") != std::string::npos,
              "BootSelfCheck planted failures: corrupt PE, missing TSV/RTTI & bad shader all fail closed (bits 0..3)");
    }

    // -------------------------------------------------------------------------
    // Check 3: 11th Console Command `selfcheck` + `help` + Cleanup
    // -------------------------------------------------------------------------
    {
        std::string respCmd, respHelp;
        const bool okCmd  = DispatchDiagnosticCommand("selfcheck", respCmd);
        const bool okHelp = DispatchDiagnosticCommand("help", respHelp);

        RawBootSelfCheckState st2{};
        const int rc2 = RAW_GetBootSelfCheckState(&st2);

        BootSelfCheckShutdown();
        LiveDashboardShutdown();
        ShaderHotReloadShutdown();
        FlightRecorderShutdown();
        AudioDirectorShutdown();
        ProfilePresetsShutdown();
        FrameGovernorShutdown();
        EngineHealthShutdown();

        std::filesystem::remove_all(tmpRoot, ec);
        const bool cleanedUp = !std::filesystem::exists(tmpRoot, ec);

        std::printf("[BootSelfCheck] Check3 ConsoleCmd: cmd='%s' runCount=%u clean=%u\n",
                    respCmd.c_str(), st2.runCount, cleanedUp ? 1u : 0u);

        Check(okCmd && okHelp && rc2 == 1 &&
              st2.runCount == 2u &&
              st2.lastReportWritten == 1u &&
              respCmd.find("[BootSelfCheck]") != std::string::npos &&
              respHelp.find("selfcheck") != std::string::npos &&
              cleanedUp,
              "BootSelfCheck `selfcheck` 11th console command runs, writes report & cleans up temp fixtures");
    }

    return localFail;
}

}  // namespace raw::rt

int run_boot_selfcheck_tests() {
    return raw::rt::RunBootSelfCheckTests(nullptr);
}

#ifdef RAW_BOOT_SELFCHECK_STANDALONE_MAIN
int main() {
    std::printf("=== RAW Wave 60: 12-Point Live Boot & Runtime Self-Diagnostic Verifier Suite ===\n");
    const int fail = run_boot_selfcheck_tests();
    std::printf("=== RESULT: %s (failures=%d) ===\n", fail == 0 ? "ALL PASS" : "FAIL", fail);
    return fail == 0 ? 0 : 1;
}
#endif
