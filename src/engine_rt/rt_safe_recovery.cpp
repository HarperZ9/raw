// rt_safe_recovery.cpp: Wave 62 Atomic Crash-Loop Safe-Rollover Recovery Guard (`raw::rt`).
// Original work, MIT.

#include "rt_safe_recovery.h"

#include "rt_profile_presets.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>

namespace raw::rt {
namespace {

struct SentinelRecord {
    bool        loaded             = false;
    bool        inProgress         = false;
    uint32_t    uncleanBootCount   = 0u;
    uint64_t    bootEpochSec       = 0ull;
    bool        safeRolloverActive = false;
    std::string activePresetName   = "Balanced";
};

struct SafeRecoveryRuntime {
    std::mutex           mu;
    bool                 initialized = false;
    std::string          outDir;
    RawSafeRecoveryState state{};
} g_sr;

void CopyStrTrunc(char* dst, size_t cap, const std::string& src) noexcept {
    if (!dst || cap == 0u) return;
    const size_t n = std::min(src.size(), cap - 1u);
    if (n > 0u) std::memcpy(dst, src.data(), n);
    dst[n] = '\0';
}

uint64_t CurrentEpochSeconds() noexcept {
    const auto now = std::chrono::system_clock::now();
    const auto sec = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    return sec > 0 ? static_cast<uint64_t>(sec) : 0ull;
}

std::string TrimLine(std::string s) {
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n' || s.back() == ' ' || s.back() == '\t')) {
        s.pop_back();
    }
    size_t a = 0u;
    while (a < s.size() && (s[a] == ' ' || s[a] == '\t')) ++a;
    return s.substr(a);
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

SentinelRecord ReadSentinelFile(const std::string& path) {
    SentinelRecord rec{};
    std::error_code ec;
    if (path.empty() || !std::filesystem::exists(path, ec)) return rec;

    std::ifstream in(path, std::ios::binary);
    if (!in) return rec;
    rec.loaded = true;

    std::string line;
    while (std::getline(in, line)) {
        line = TrimLine(line);
        if (line.empty() || line[0] == '#') continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = TrimLine(line.substr(0u, eq));
        const std::string v = TrimLine(line.substr(eq + 1u));
        if (k == "in_progress") {
            rec.inProgress = (v == "1" || v == "true");
        } else if (k == "unclean_boot_count") {
            rec.uncleanBootCount = static_cast<uint32_t>(std::strtoul(v.c_str(), nullptr, 10));
        } else if (k == "boot_epoch_sec") {
            rec.bootEpochSec = static_cast<uint64_t>(std::strtoull(v.c_str(), nullptr, 10));
        } else if (k == "safe_rollover_active") {
            rec.safeRolloverActive = (v == "1" || v == "true");
        } else if (k == "active_preset" && !v.empty()) {
            rec.activePresetName = v;
        }
    }
    return rec;
}

std::string FormatSentinelContent(uint64_t bootEpochSec,
                                  const char* presetName,
                                  uint32_t uncleanCount,
                                  bool rolloverActive,
                                  bool inProgress) {
    char buf[320];
    std::snprintf(buf, sizeof(buf),
                  "# RAW Engine Boot Sentinel (cleared on clean shutdown)\n"
                  "sentinel_version=1\n"
                  "in_progress=%u\n"
                  "unclean_boot_count=%u\n"
                  "boot_epoch_sec=%llu\n"
                  "active_preset=%s\n"
                  "safe_rollover_active=%u\n",
                  inProgress ? 1u : 0u,
                  uncleanCount,
                  static_cast<unsigned long long>(bootEpochSec),
                  (presetName && presetName[0]) ? presetName : "Balanced",
                  rolloverActive ? 1u : 0u);
    return buf;
}

std::string FormatRecoveryReport(uint32_t uncleanCount,
                                 uint64_t bootEpochSec,
                                 const std::string& prevPreset) {
    std::ostringstream os;
    char buf[256];
    os << "========================================================================\n"
       << "RAW ENGINE RUNTIME - AUTOMATIC CRASH-LOOP SAFEMODE ROLLOVER REPORT\n"
       << "========================================================================\n";
    std::snprintf(buf, sizeof(buf),
                  "BootEpochSec          : %llu\n"
                  "ConsecutiveUnclean    : %u (threshold=%u)\n"
                  "PreviousPreset        : %s\n"
                  "EnforcedRecoveryPreset: SafeMode\n"
                  "ExperimentalOverrides : DISABLED\n\n",
                  static_cast<unsigned long long>(bootEpochSec),
                  uncleanCount, kSafeRecoveryCrashThreshold,
                  prevPreset.empty() ? "Unknown" : prevPreset.c_str());
    os << buf;
    os << "Action Taken:\n"
       << "  - Detected >= 2 consecutive unclean sessions without clean RAW_ModuleStop().\n"
       << "  - Automatically forced QualityPreset::SafeMode (dynamic governor, XPBD cloth/hair,\n"
       << "    camera FOV breath, and adaptive audio director disabled; crash forensics and\n"
       << "    flight recorder remain active for diagnostics).\n"
       << "Recovery Instructions:\n"
       << "  - Exit Skyrim SE cleanly to automatically clear boot_sentinel.state, OR\n"
       << "  - Write 'recovery reset' to raw_engine_out\\raw_cmd.txt to clear the counter live.\n";
    return os.str();
}

}  // namespace

std::string SafeRecoveryInit(const std::string& outDir) {
    const std::string sentPath = outDir.empty()
                                     ? std::string()
                                     : (std::filesystem::path(outDir) / "boot_sentinel.state").string();
    const std::string repPath  = outDir.empty()
                                     ? std::string()
                                     : (std::filesystem::path(outDir) / "recovery_report.txt").string();

    const SentinelRecord prev = ReadSentinelFile(sentPath);
    const uint64_t nowEpoch   = CurrentEpochSeconds();

    // If previous boot left in_progress=1, increment consecutive unclean boot count!
    const uint32_t uncleanCount = (prev.loaded && prev.inProgress)
                                      ? (prev.uncleanBootCount + 1u)
                                      : 0u;
    const bool triggerRollover = (uncleanCount >= kSafeRecoveryCrashThreshold);

    // Query current active preset before potential SafeMode rollover.
    RawProfilePresetState pp{};
    (void)RAW_GetProfilePresetState(&pp);
    std::string activePresetStr = QualityPresetName(static_cast<QualityPreset>(pp.activePreset));

    if (triggerRollover) {
        (void)ApplyActiveQualityPreset(QualityPreset::SafeMode, nullptr);
        activePresetStr = QualityPresetName(QualityPreset::SafeMode);
        if (!repPath.empty()) {
            const std::string repText = FormatRecoveryReport(uncleanCount, nowEpoch, prev.activePresetName);
            (void)AtomicWriteTextFile(repPath, repText);
        }
    }

    const bool wroteSentinel = !sentPath.empty() &&
                               AtomicWriteTextFile(
                                   sentPath,
                                   FormatSentinelContent(nowEpoch, activePresetStr.c_str(),
                                                         uncleanCount, triggerRollover, true));

    {
        std::lock_guard<std::mutex> lk(g_sr.mu);
        const uint32_t prevRollovers = g_sr.state.totalRollovers;
        const uint32_t prevResets    = g_sr.state.manualResets;
        g_sr.initialized                    = true;
        g_sr.outDir                         = outDir;
        g_sr.state                          = {};
        g_sr.state.guardActive              = 1u;
        g_sr.state.sentinelPresent          = wroteSentinel ? 1u : 0u;
        g_sr.state.safeRolloverActive       = triggerRollover ? 1u : 0u;
        g_sr.state.experimentalDisabled     = triggerRollover ? 1u : 0u;
        g_sr.state.uncleanBootCount         = uncleanCount;
        g_sr.state.crashLoopThreshold       = kSafeRecoveryCrashThreshold;
        g_sr.state.totalRollovers           = prevRollovers + (triggerRollover ? 1u : 0u);
        g_sr.state.manualResets             = prevResets;
        g_sr.state.bootEpochSec             = nowEpoch;
        CopyStrTrunc(g_sr.state.activePresetName, sizeof(g_sr.state.activePresetName), activePresetStr);
        CopyStrTrunc(g_sr.state.sentinelPath, sizeof(g_sr.state.sentinelPath), sentPath);
        CopyStrTrunc(g_sr.state.recoveryReportPath, sizeof(g_sr.state.recoveryReportPath), triggerRollover ? repPath : std::string());
    }

    char buf[288];
    std::snprintf(buf, sizeof(buf),
                  "[SafeRecovery] initialized (uncleanBoots=%u/%u rollover=%s preset=%s sentinel=%s)",
                  uncleanCount, kSafeRecoveryCrashThreshold,
                  triggerRollover ? "SAFEMODE_ENFORCED" : "normal",
                  activePresetStr.c_str(),
                  wroteSentinel ? "armed" : "none");
    return buf;
}

bool SafeRecoveryResetCounter(std::string* outSummary) {
    std::string sentPath;
    uint64_t epochSec = 0ull;
    std::string presetName;
    uint32_t resets = 0u;
    {
        std::lock_guard<std::mutex> lk(g_sr.mu);
        g_sr.state.uncleanBootCount     = 0u;
        g_sr.state.safeRolloverActive   = 0u;
        g_sr.state.experimentalDisabled = 0u;
        ++g_sr.state.manualResets;
        resets     = g_sr.state.manualResets;
        sentPath   = g_sr.state.sentinelPath;
        epochSec   = g_sr.state.bootEpochSec;
        presetName = g_sr.state.activePresetName;
    }

    bool rewritten = false;
    if (!sentPath.empty()) {
        rewritten = AtomicWriteTextFile(
            sentPath,
            FormatSentinelContent(epochSec, presetName.c_str(), 0u, false, true));
        std::lock_guard<std::mutex> lk(g_sr.mu);
        g_sr.state.sentinelPresent = rewritten ? 1u : 0u;
    }

    if (outSummary) {
        char buf[192];
        std::snprintf(buf, sizeof(buf),
                      "[SafeRecovery] counter reset (uncleanBoots=0/%u rollover=off manualResets=%u sentinel=%s)",
                      kSafeRecoveryCrashThreshold, resets, rewritten ? "updated" : "none");
        *outSummary = buf;
    }
    return true;
}

std::string SafeRecoveryFormatStatus() {
    RawSafeRecoveryState st{};
    (void)RAW_GetSafeRecoveryState(&st);
    char buf[320];
    std::snprintf(buf, sizeof(buf),
                  "[SafeRecovery] active=%u uncleanBoots=%u/%u safeRollover=%u expDisabled=%u preset=%s sentinel=%u rollovers=%u resets=%u",
                  static_cast<unsigned>(st.guardActive),
                  st.uncleanBootCount, st.crashLoopThreshold,
                  static_cast<unsigned>(st.safeRolloverActive),
                  static_cast<unsigned>(st.experimentalDisabled),
                  st.activePresetName[0] ? st.activePresetName : "Balanced",
                  static_cast<unsigned>(st.sentinelPresent),
                  st.totalRollovers, st.manualResets);
    return buf;
}

void SafeRecoveryShutdown() {
    std::string sentPath;
    {
        std::lock_guard<std::mutex> lk(g_sr.mu);
        sentPath                     = g_sr.state.sentinelPath;
        g_sr.initialized             = false;
        g_sr.outDir.clear();
        g_sr.state.guardActive       = 0u;
        g_sr.state.sentinelPresent   = 0u;
        g_sr.state.uncleanBootCount  = 0u;
    }
    if (!sentPath.empty()) {
        std::error_code ec;
        std::filesystem::remove(sentPath, ec);
    }
}

}  // namespace raw::rt

extern "C" __declspec(dllexport) int RAW_GetSafeRecoveryState(raw::rt::RawSafeRecoveryState* out) {
    if (!out) return 0;
    std::lock_guard<std::mutex> lk(raw::rt::g_sr.mu);
    *out = raw::rt::g_sr.state;
    return raw::rt::g_sr.initialized ? 1 : 0;
}
