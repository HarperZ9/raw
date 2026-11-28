// rt_console_diag.cpp: Wave 56 Live File-Triggered Engine Diagnostic Command Dispatcher (`raw::rt`).
// Original work, MIT.

#include "rt_console_diag.h"

#include "rt_adaptive_audio_director.h"
#include "rt_boot_selfcheck.h"
#include "rt_engine_health.h"
#include "rt_frame_governor.h"
#include "rt_hotkey_router.h"
#include "rt_live_dashboard.h"
#include "rt_mod_load_audit.h"
#include "rt_profile_presets.h"
#include "rt_safe_recovery.h"
#include "rt_save_papyrus_guard.h"
#include "rt_sdk_bridge.h"
#include "rt_shader_hotreload.h"
#include "rt_telemetry_recorder.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>

namespace raw::rt {
namespace {

constexpr uint32_t kPollCadenceFrames = 60u;
constexpr size_t   kMaxCmdFileBytes   = 1024u;

struct ConsoleDiagRuntime {
    std::mutex  mu;
    bool        initialized      = false;
    std::string outDir;
    uint32_t    presentCounter   = 0u;
    uint32_t    commandsExecuted = 0u;
    uint32_t    batchesProcessed = 0u;
} g_cd;

std::string TrimWhitespace(const std::string& s) {
    size_t a = 0u;
    while (a < s.size() && std::isspace(static_cast<unsigned char>(s[a])) != 0) ++a;
    size_t b = s.size();
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1u])) != 0) --b;
    return s.substr(a, b - a);
}

std::string ToLowerAscii(std::string s) {
    for (char& ch : s) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return s;
}

uint32_t PopCount32(uint32_t v) noexcept {
    uint32_t c = 0u;
    while (v != 0u) {
        v &= (v - 1u);
        ++c;
    }
    return c;
}

bool AtomicWriteTextFile(const std::string& outPath, const std::string& content) {
    if (outPath.empty()) return false;
    try {
        const std::filesystem::path dst(outPath);
        if (dst.has_parent_path()) {
            std::error_code ec;
            std::filesystem::create_directories(dst.parent_path(), ec);
        }
        const std::string tmpPath = outPath + ".tmp";
        {
            std::ofstream out(tmpPath, std::ios::binary | std::ios::trunc);
            if (!out) return false;
            out.write(content.data(), static_cast<std::streamsize>(content.size()));
            if (!out.good()) return false;
        }
        std::error_code ec;
        std::filesystem::rename(tmpPath, dst, ec);
        if (ec) {
            std::filesystem::remove(dst, ec);
            ec.clear();
            std::filesystem::rename(tmpPath, dst, ec);
            if (ec) {
                std::ofstream direct(outPath, std::ios::binary | std::ios::trunc);
                if (!direct) return false;
                direct.write(content.data(), static_cast<std::streamsize>(content.size()));
                std::filesystem::remove(tmpPath, ec);
                return direct.good();
            }
        }
        return true;
    } catch (...) {
        return false;
    }
}

void ProcessCommandFile(const std::string& outDir) {
    try {
        const std::filesystem::path cmdPath = std::filesystem::path(outDir) / "raw_cmd.txt";
        std::error_code ec;
        if (!std::filesystem::exists(cmdPath, ec) || ec) return;
        if (!std::filesystem::is_regular_file(cmdPath, ec) || ec) return;

        // Read bounded up to 1 KB (1024 bytes) so hostile/huge files cannot stall Present.
        std::string payload;
        {
            std::ifstream in(cmdPath, std::ios::binary);
            if (!in) return;
            char buf[kMaxCmdFileBytes];
            in.read(buf, static_cast<std::streamsize>(kMaxCmdFileBytes));
            const std::streamsize got = in.gcount();
            if (got > 0) {
                payload.assign(buf, static_cast<size_t>(got));
            }
        }

        // Delete trigger file immediately after read so the batch runs strictly once.
        std::filesystem::remove(cmdPath, ec);

        std::ostringstream report;
        report << "=== RAW ENGINE DIAGNOSTIC COMMAND RESULT ===\n";

        std::istringstream lines(payload);
        std::string rawLine;
        uint32_t ranCount = 0u;
        while (std::getline(lines, rawLine)) {
            const std::string trimmed = TrimWhitespace(rawLine);
            if (trimmed.empty()) continue;
            if (trimmed[0] == '#' || trimmed[0] == ';') continue;

            std::string resp;
            const bool ok = DispatchDiagnosticCommand(trimmed.c_str(), resp);
            report << "[" << trimmed << "] (" << (ok ? "OK" : "ERR") << ")\n";
            report << resp;
            if (resp.empty() || resp.back() != '\n') report << "\n";
            ++ranCount;
        }

        if (ranCount == 0u) {
            report << "(no executable commands found; write 'help' to list commands)\n";
        }

        const std::filesystem::path resPath = std::filesystem::path(outDir) / "raw_cmd_result.txt";
        (void)AtomicWriteTextFile(resPath.string(), report.str());

        std::lock_guard<std::mutex> lk(g_cd.mu);
        g_cd.commandsExecuted += ranCount;
        ++g_cd.batchesProcessed;
    } catch (...) {
        // Never allow diagnostic file I/O to propagate into Present.
    }
}

}  // namespace

bool DispatchDiagnosticCommand(const char* cmdLine, std::string& outResponse) noexcept {
    try {
        outResponse.clear();
        if (!cmdLine) {
            outResponse = "Error: null command. Type 'help' for available commands (help, health, sdk, flight, mods, saves, status).";
            return false;
        }

        const std::string trimmed = TrimWhitespace(std::string(cmdLine));
        if (trimmed.empty()) {
            outResponse = "Error: empty command. Type 'help' for available commands (help, health, sdk, flight, mods, saves, status).";
            return false;
        }

        // Extract primary verb (first whitespace-delimited token) and optional argument.
        size_t sp = 0u;
        while (sp < trimmed.size() && std::isspace(static_cast<unsigned char>(trimmed[sp])) == 0) ++sp;
        const std::string verb = ToLowerAscii(trimmed.substr(0u, sp));
        const std::string arg  = (sp < trimmed.size()) ? TrimWhitespace(trimmed.substr(sp)) : std::string();

        if (verb == "help") {
            outResponse =
                "RAW Engine Diagnostic Commands (write 1 command per line to raw_engine_out\\raw_cmd.txt):\n"
                "  help   - List all diagnostic commands and syntax\n"
                "  health - Query 0-100 engine health score, grade, pillars & advisories\n"
                "  sdk    - Flush unified_sdk_snapshot.json & report SDK version and 17-subsystem mask\n"
                "  flight - Flush session_flight_recorder.json & report p50/p95/p99 frame stats and hitches\n"
                "  mods   - Report TES4 mod load-order plugin/ESL counts and integrity issues\n"
                "  saves  - Report .ESS save Papyrus string-table health & frame hitch watchdog telemetry\n"
                "  status - Concise unified snapshot (health, governor, audio mood, weather & GMST modules)\n"
                "  preset <ultra|balanced|competitive|safemode> - Query or apply a validated 4-tier quality preset\n"
                "  reload_shaders - Run atomic pre-validation & trigger raw_shaders\\ hot-reload generation\n"
                "  dashboard - Atomically export raw_live_dashboard.json & self-contained raw_live_dashboard.html\n"
                "  selfcheck - Run 12-point boot & runtime integrity verifier and write boot_selfcheck.txt\n"
                "  hotkeys - Report foreground-safe debounced live hotkey bindings and trigger counters\n"
                "  recovery [status|reset] - Query or reset crash-loop SafeMode rollover guard state";
            return true;
        }

        if (verb == "recovery") {
            const std::string sub = ToLowerAscii(arg);
            if (sub.empty() || sub == "status") {
                outResponse = SafeRecoveryFormatStatus();
                return true;
            }
            if (sub == "reset") {
                std::string summary;
                (void)SafeRecoveryResetCounter(&summary);
                outResponse = summary;
                return true;
            }
            outResponse = "Error: unknown recovery argument '" + arg +
                          "'. Usage: recovery [status|reset] (type 'help' for commands).";
            return false;
        }

        if (verb == "hotkeys") {
            outResponse = HotkeyRouterFormatBindings();
            return true;
        }

        if (verb == "selfcheck") {
            std::string summary;
            (void)BootSelfCheckRunAndWrite(nullptr, &summary);
            outResponse = summary;
            return true;
        }

        if (verb == "dashboard") {
            std::string summary;
            (void)LiveDashboardExportNow(nullptr, &summary);
            outResponse = summary;
            return true;
        }

        if (verb == "reload_shaders") {
            std::string summary;
            (void)ShaderHotReloadTriggerNow(&summary);
            outResponse = summary;
            return true;
        }

        if (verb == "preset") {
            if (arg.empty()) {
                RawProfilePresetState st{};
                const int active = RAW_GetProfilePresetState(&st);
                char buf[256];
                std::snprintf(buf, sizeof(buf),
                              "[Preset] active=%s live=%d targetFps=%.0f renderFloor=%.2f | watchdog=%s (RAM=%.0f%% VRAM=%.0f%% rec=%s) | usage: preset <ultra|balanced|competitive|safemode>",
                              QualityPresetName(static_cast<QualityPreset>(st.activePreset)),
                              active, double(st.targetFps), double(st.renderScaleFloor),
                              BudgetPressureName(static_cast<BudgetPressureLevel>(st.overallPressure)),
                              double(st.ramUtilizationPct), double(st.vramUtilizationPct),
                              QualityPresetName(static_cast<QualityPreset>(st.recommendedPreset)));
                outResponse = buf;
                return true;
            }
            QualityPreset target = QualityPreset::Balanced;
            if (!ParseQualityPresetName(arg.c_str(), target)) {
                outResponse = "Error: unknown preset '" + arg +
                              "'. Usage: preset <ultra|balanced|competitive|safemode> (type 'help' for commands).";
                return false;
            }
            std::string summary;
            if (!ApplyActiveQualityPreset(target, &summary)) {
                outResponse = "Error: failed to apply preset '" + arg + "'.";
                return false;
            }
            outResponse = summary;
            return true;
        }

        if (verb == "health") {
            RawEngineHealthReport rep{};
            const int active = RAW_GetEngineHealthReport(&rep);
            std::ostringstream os;
            os << EngineHealthFormatSummary(rep) << " | live=" << (active ? 1 : 0);
            outResponse = os.str();
            return true;
        }

        if (verb == "sdk") {
            const bool flushed = SdkBridgeExportSnapshotJson(nullptr);
            RawUnifiedEngineSnapshot snap{};
            snap.structSize = static_cast<uint32_t>(sizeof(RawUnifiedEngineSnapshot));
            const int ok = RAW_GetUnifiedEngineSnapshot(&snap);
            char buf[256];
            std::snprintf(buf, sizeof(buf),
                          "[SDK] version=0x%08X (v%u.%u.%u) ok=%d activeSubsystems=%u/17 (0x%05X) gmstModules=%u jsonFlushed=%s",
                          RAW_GetSdkVersion(),
                          RAW_ENGINE_SDK_VERSION_MAJOR, RAW_ENGINE_SDK_VERSION_MINOR, RAW_ENGINE_SDK_VERSION_PATCH,
                          ok, PopCount32(snap.activeSubsystemMask), snap.activeSubsystemMask,
                          snap.verifiedGmstModules, flushed ? "yes" : "no");
            outResponse = buf;
            return true;
        }

        if (verb == "flight") {
            const bool flushed = FlightRecorderFlushJson(nullptr);
            RawFlightRecorderStats frs{};
            const int active = RAW_GetFlightRecorderStats(&frs);
            char buf[256];
            std::snprintf(buf, sizeof(buf),
                          "[Flight] active=%d frames=%llu ring=%u/%u mean=%.2fms p50=%.2fms p95=%.2fms p99=%.2fms max=%.2fms hitches(>50ms)=%u jsonFlushed=%s",
                          active,
                          static_cast<unsigned long long>(frs.totalRecordedFrames),
                          frs.ringFillCount, static_cast<unsigned>(frs.ringCapacity),
                          double(frs.meanFrameMs), double(frs.p50FrameMs),
                          double(frs.p95FrameMs), double(frs.p99FrameMs),
                          double(frs.maxFrameMs), frs.hitchFramesOver50ms,
                          flushed ? "yes" : "no");
            outResponse = buf;
            return true;
        }

        if (verb == "mods") {
            RawModAuditSummary mas{};
            const int active = RAW_GetModAuditSummary(&mas);
            const uint32_t issues =
                static_cast<uint32_t>(mas.missingMasterCount) +
                static_cast<uint32_t>(mas.outOfOrderMasterCount) +
                static_cast<uint32_t>(mas.corruptHeaderCount) +
                static_cast<uint32_t>(mas.oversizedEslCount) +
                static_cast<uint32_t>(mas.fullSlotOverflow) +
                static_cast<uint32_t>(mas.lightSlotOverflow);
            char buf[256];
            std::snprintf(buf, sizeof(buf),
                          "[Mods] active=%d severity=%s total=%u full=%u/254 esl=%u/4096 issues=%u (missing=%u outOfOrder=%u corrupt=%u oversizedEsl=%u)",
                          active,
                          ModAuditSeverityName(static_cast<ModAuditSeverity>(mas.severity)),
                          static_cast<unsigned>(mas.totalPlugins),
                          static_cast<unsigned>(mas.fullPluginCount),
                          static_cast<unsigned>(mas.lightEslCount),
                          issues,
                          static_cast<unsigned>(mas.missingMasterCount),
                          static_cast<unsigned>(mas.outOfOrderMasterCount),
                          static_cast<unsigned>(mas.corruptHeaderCount),
                          static_cast<unsigned>(mas.oversizedEslCount));
            outResponse = buf;
            return true;
        }

        if (verb == "saves") {
            RawSavePapyrusHealth sph{};
            const int active = RAW_GetSavePapyrusHealth(&sph);
            char buf[256];
            std::snprintf(buf, sizeof(buf),
                          "[Saves] active=%d audited=%u worstHealth=%s maxStrings=%u/65535 maxSize=%.2fMB | hitches(>50ms)=%u (60s=%u) worst=%.1fms p95=%.1fms",
                          active,
                          static_cast<unsigned>(sph.savesAudited),
                          SaveHealthLevelName(static_cast<SaveHealthLevel>(sph.worstSaveHealth)),
                          sph.worstStringCount, double(sph.worstSaveSizeMb),
                          sph.hitchCount, sph.hitchesLast60Sec,
                          double(sph.worstHitchMs), double(sph.p95FrameMs));
            outResponse = buf;
            return true;
        }

        if (verb == "status") {
            RawUnifiedEngineSnapshot snap{};
            snap.structSize = static_cast<uint32_t>(sizeof(RawUnifiedEngineSnapshot));
            (void)RAW_GetUnifiedEngineSnapshot(&snap);
            const char* gradeStr  = HealthGradeName(static_cast<HealthGrade>(snap.healthGrade));
            const char* govStr    = GovernorRegimeName(static_cast<GovernorRegime>(snap.governorRegime));
            const char* moodStr   = AudioMoodName(static_cast<AudioMood>(snap.audioMood));
            char buf[384];
            std::snprintf(buf, sizeof(buf),
                          "[Status] Health=%d/100(%s) | Gov=%s(render=%.2f vol=%.2f gi=%.2f p95=%.2fms) | Audio=%s(lpf=%.0fHz) | Weather=%.1fC snow=%.2fm wind=Bft%.1f | Handles=%u | GMST=%u/8 Subsys=%u/17",
                          snap.healthScore, gradeStr,
                          govStr, double(snap.renderScale), double(snap.volumetricQuality01),
                          double(snap.giRayBudget01), double(snap.frameP95Ms),
                          moodStr, double(snap.audioLpfCutoffHz),
                          double(snap.surfaceTempC), double(snap.snowpackM), double(snap.windBeaufort),
                          snap.activeHandles, snap.verifiedGmstModules,
                          PopCount32(snap.activeSubsystemMask));
            outResponse = buf;
            return true;
        }

        outResponse = "Error: unknown command '" + trimmed +
                      "'. Type 'help' for available commands (help, health, sdk, flight, mods, saves, status).";
        return false;
    } catch (...) {
        outResponse = "Error: internal diagnostic dispatcher failure.";
        return false;
    }
}

std::string ConsoleDiagInit(const std::string& outDir) {
    {
        std::lock_guard<std::mutex> lk(g_cd.mu);
        g_cd.initialized      = true;
        g_cd.outDir           = outDir;
        g_cd.presentCounter   = 0u;
        g_cd.commandsExecuted = 0u;
        g_cd.batchesProcessed = 0u;
    }
    char buf[224];
    std::snprintf(buf, sizeof(buf),
                  "[ConsoleDiag] initialized (13 commands: help/health/sdk/flight/mods/saves/status/preset/reload_shaders/dashboard/selfcheck/hotkeys/recovery | poll=%uf -> %s\\raw_cmd.txt)",
                  kPollCadenceFrames, outDir.empty() ? "raw_engine_out" : outDir.c_str());
    return buf;
}

void ConsoleDiagOnPresent() {
    std::string outDirCopy;
    {
        std::lock_guard<std::mutex> lk(g_cd.mu);
        if (!g_cd.initialized || g_cd.outDir.empty()) return;
        ++g_cd.presentCounter;
        if ((g_cd.presentCounter % kPollCadenceFrames) != 0u) return;
        outDirCopy = g_cd.outDir;
    }
    ProcessCommandFile(outDirCopy);
}

void ConsoleDiagShutdown() {
    std::lock_guard<std::mutex> lk(g_cd.mu);
    g_cd.initialized    = false;
    g_cd.outDir.clear();
    g_cd.presentCounter = 0u;
}

uint32_t ConsoleDiagCommandsExecuted() noexcept {
    std::lock_guard<std::mutex> lk(g_cd.mu);
    return g_cd.commandsExecuted;
}

uint32_t ConsoleDiagBatchesProcessed() noexcept {
    std::lock_guard<std::mutex> lk(g_cd.mu);
    return g_cd.batchesProcessed;
}

}  // namespace raw::rt
