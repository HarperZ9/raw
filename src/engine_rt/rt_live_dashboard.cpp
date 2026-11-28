// rt_live_dashboard.cpp: Wave 59 Non-Blocking Live JSON/HTML Telemetry Snapshot Dashboard (`raw::rt`).
// Original work, MIT.

#include "rt_live_dashboard.h"

#include "rt_adaptive_audio_director.h"
#include "rt_engine_health.h"
#include "rt_frame_governor.h"
#include "rt_profile_presets.h"
#include "rt_sdk_bridge.h"
#include "rt_shader_hotreload.h"
#include "rt_telemetry_recorder.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>

namespace raw::rt {
namespace {

inline float SafeF(float v, float fb) noexcept {
    return std::isfinite(v) ? v : fb;
}

uint32_t PopCount32(uint32_t v) noexcept {
    uint32_t c = 0u;
    while (v != 0u) {
        v &= (v - 1u);
        ++c;
    }
    return c;
}

void CopyTrunc(char* dst, size_t cap, const std::string& src) noexcept {
    if (!dst || cap == 0u) return;
    const size_t n = std::min(src.size(), cap - 1u);
    if (n > 0u) std::memcpy(dst, src.data(), n);
    dst[n] = '\0';
}

std::string EscapeJson(const char* s) {
    std::string out;
    if (!s) return out;
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(s); *p != '\0'; ++p) {
        const unsigned char c = *p;
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (c < 0x20u) {
                    char u[8];
                    std::snprintf(u, sizeof(u), "\\u%04x", static_cast<unsigned>(c));
                    out += u;
                } else {
                    out.push_back(static_cast<char>(c));
                }
                break;
        }
    }
    return out;
}

std::string EscapeHtml(const char* s) {
    std::string out;
    if (!s) return out;
    for (const char* p = s; *p != '\0'; ++p) {
        switch (*p) {
            case '&':  out += "&amp;";  break;
            case '<':  out += "&lt;";   break;
            case '>':  out += "&gt;";   break;
            case '"':  out += "&quot;"; break;
            case '\'': out += "&#39;";  break;
            default:   out.push_back(*p); break;
        }
    }
    return out;
}

bool AtomicWriteStringFile(const std::string& outPath, const std::string& content) {
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

struct LiveDashboardRuntime {
    std::mutex            mu;
    bool                  initialized = false;
    std::string           outDir;
    uint32_t              presentTick = 0u;
    RawLiveDashboardState state{};
} g_ld;

}  // namespace

LiveDashboardSnapshot CaptureLiveDashboardSnapshot(uint32_t exportNumber) noexcept {
    LiveDashboardSnapshot d{};
    d.exportNumber = exportNumber;
    d.sdkVersion   = RAW_GetSdkVersion();

    // 1. rt_sdk_bridge unified snapshot
    {
        RawUnifiedEngineSnapshot u{};
        u.structSize = static_cast<uint32_t>(sizeof(RawUnifiedEngineSnapshot));
        if (RAW_GetUnifiedEngineSnapshot(&u)) {
            d.sdkVersion          = u.sdkVersion;
            d.activeSubsystemMask = u.activeSubsystemMask;
            d.verifiedGmstModules = u.verifiedGmstModules;
            d.surfaceTempC        = SafeF(u.surfaceTempC, 12.0f);
            d.snowpackM           = SafeF(u.snowpackM, 0.0f);
            d.windBeaufort        = SafeF(u.windBeaufort, 2.0f);
            d.activeHandles       = u.activeHandles;
            d.saveMaxStrings      = u.saveMaxStrings;
            d.modPluginCount      = u.modPluginCount;
            d.modIssueCount       = u.modIssueCount;
        }
    }

    // 2. rt_engine_health
    {
        RawEngineHealthReport rep{};
        (void)RAW_GetEngineHealthReport(&rep);
        d.healthScore   = rep.compositeScore;
        d.healthGrade   = rep.grade;
        d.pillarBinary  = rep.scoreBinaryOffsets;
        d.pillarHandles = rep.scoreRefHandles;
        d.pillarSaves   = rep.scoreSaveHealth;
        d.pillarMods    = rep.scoreModOrder;
        d.pillarPacing  = rep.scoreFramePacing;
        d.pillarDiag    = rep.scoreDiagnostics;
        d.advisoryCount = std::min<uint8_t>(rep.advisoryCount, 4u);
        for (uint8_t i = 0u; i < d.advisoryCount; ++i) {
            std::memcpy(d.advisories[i], rep.advisories[i], sizeof(d.advisories[i]));
            d.advisories[i][sizeof(d.advisories[i]) - 1u] = '\0';
        }
    }

    // 3. rt_telemetry_recorder
    {
        RawFlightRecorderStats fr{};
        (void)RAW_GetFlightRecorderStats(&fr);
        d.totalRecordedFrames = fr.totalRecordedFrames;
        d.ringFillCount       = fr.ringFillCount;
        d.hitchFramesOver50ms = fr.hitchFramesOver50ms;
        if (fr.ringFillCount > 0u) {
            d.meanFrameMs = SafeF(fr.meanFrameMs, 16.67f);
            d.p50FrameMs  = SafeF(fr.p50FrameMs, 16.67f);
            d.p95FrameMs  = SafeF(fr.p95FrameMs, 16.67f);
            d.p99FrameMs  = SafeF(fr.p99FrameMs, 16.67f);
            d.maxFrameMs  = SafeF(fr.maxFrameMs, 16.67f);
        }
    }

    // 4. rt_frame_governor
    {
        RawFrameGovernorState fg{};
        (void)RAW_GetFrameGovernorState(&fg);
        d.governorEnabled     = fg.governorEnabled;
        d.governorRegime      = fg.regime;
        d.targetFps           = SafeF(fg.targetFps, 60.0f);
        d.emaFrameMs          = SafeF(fg.emaFrameMs, 16.67f);
        d.renderScale         = SafeF(fg.renderScale, 1.0f);
        d.volumetricQuality01 = SafeF(fg.volumetricQuality01, 1.0f);
        d.shadowDistanceScale = SafeF(fg.shadowDistanceScale, 1.0f);
        d.floraLodScale       = SafeF(fg.floraLodScale, 1.0f);
        d.giRayBudget01       = SafeF(fg.giRayBudget01, 1.0f);
    }

    // 5. rt_profile_presets
    {
        RawProfilePresetState pp{};
        (void)RAW_GetProfilePresetState(&pp);
        d.activePreset        = pp.activePreset;
        d.recommendedPreset   = pp.recommendedPreset;
        d.overallPressure     = pp.overallPressure;
        d.fallbackRecommended = pp.fallbackRecommended;
        d.ramUtilizationPct   = SafeF(pp.ramUtilizationPct, 0.0f);
        d.vramUtilizationPct  = SafeF(pp.vramUtilizationPct, 0.0f);
        d.vramUsedMB          = SafeF(pp.vramUsedMB, 0.0f);
        d.vramBudgetMB        = SafeF(pp.vramBudgetMB, 6144.0f);
    }

    // 6. rt_shader_hotreload
    {
        RawShaderHotReloadState shr{};
        (void)RAW_GetShaderHotReloadState(&shr);
        d.shaderWatcherActive = shr.watcherActive;
        d.shaderReloadGen     = shr.reloadGeneration;
        d.shaderTrackedCount  = shr.trackedFileCount;
        d.shaderDirtyCount    = shr.dirtyFileCount;
        d.shaderAcceptedCount = shr.acceptedReloads;
        d.shaderRejectedCount = shr.rejectedEdits;
        std::memcpy(d.lastDirtyShader, shr.lastDirtyShader, sizeof(d.lastDirtyShader));
        d.lastDirtyShader[sizeof(d.lastDirtyShader) - 1u] = '\0';
    }

    // 7. rt_adaptive_audio_director
    {
        RawAudioDirectorState ad{};
        (void)RAW_GetAudioDirectorState(&ad);
        d.audioMood          = ad.activeMood;
        d.audioLpfCutoffHz   = SafeF(ad.lpfCutoffHz, 18000.0f);
        d.audioReverbWetMult = SafeF(ad.reverbWetMult, 0.85f);
        d.audioDuckGain      = SafeF(ad.dialogueDuckGain, 1.0f);
        d.audioTransitions   = ad.transitionCount;
    }

    return d;
}

std::string FormatLiveDashboardJson(const LiveDashboardSnapshot& s) {
    std::ostringstream os;
    char buf[512];

    os << "{\n";
    std::snprintf(buf, sizeof(buf),
                  "  \"dashboardVersion\": \"1.0.0\",\n"
                  "  \"exportCount\": %u,\n"
                  "  \"sdk\": {\n"
                  "    \"version\": %u,\n"
                  "    \"versionHex\": \"0x%08X\",\n"
                  "    \"activeSubsystemMask\": %u,\n"
                  "    \"activeSubsystemMaskHex\": \"0x%05X\",\n"
                  "    \"activeSubsystemCount\": %u,\n"
                  "    \"verifiedGmstModules\": %u\n"
                  "  },\n",
                  s.exportNumber, s.sdkVersion, s.sdkVersion,
                  s.activeSubsystemMask, s.activeSubsystemMask,
                  PopCount32(s.activeSubsystemMask), s.verifiedGmstModules);
    os << buf;

    std::snprintf(buf, sizeof(buf),
                  "  \"health\": {\n"
                  "    \"score\": %u,\n"
                  "    \"grade\": \"%s\",\n"
                  "    \"pillars\": { \"binary\": %u, \"handles\": %u, \"saves\": %u, \"mods\": %u, \"pacing\": %u, \"diagnostics\": %u },\n"
                  "    \"advisories\": [",
                  static_cast<unsigned>(s.healthScore),
                  HealthGradeName(static_cast<HealthGrade>(s.healthGrade)),
                  static_cast<unsigned>(s.pillarBinary),
                  static_cast<unsigned>(s.pillarHandles),
                  static_cast<unsigned>(s.pillarSaves),
                  static_cast<unsigned>(s.pillarMods),
                  static_cast<unsigned>(s.pillarPacing),
                  static_cast<unsigned>(s.pillarDiag));
    os << buf;
    for (uint8_t i = 0u; i < s.advisoryCount && i < 4u; ++i) {
        if (i > 0u) os << ", ";
        os << "\"" << EscapeJson(s.advisories[i]) << "\"";
    }
    os << "]\n  },\n";

    std::snprintf(buf, sizeof(buf),
                  "  \"framePacing\": {\n"
                  "    \"totalRecordedFrames\": %llu,\n"
                  "    \"ringFillCount\": %u,\n"
                  "    \"hitchesOver50ms\": %u,\n"
                  "    \"meanMs\": %.3f,\n"
                  "    \"p50Ms\": %.3f,\n"
                  "    \"p95Ms\": %.3f,\n"
                  "    \"p99Ms\": %.3f,\n"
                  "    \"maxMs\": %.3f\n"
                  "  },\n",
                  static_cast<unsigned long long>(s.totalRecordedFrames),
                  s.ringFillCount, s.hitchFramesOver50ms,
                  double(SafeF(s.meanFrameMs, 16.67f)),
                  double(SafeF(s.p50FrameMs, 16.67f)),
                  double(SafeF(s.p95FrameMs, 16.67f)),
                  double(SafeF(s.p99FrameMs, 16.67f)),
                  double(SafeF(s.maxFrameMs, 16.67f)));
    os << buf;

    std::snprintf(buf, sizeof(buf),
                  "  \"governor\": {\n"
                  "    \"enabled\": %u,\n"
                  "    \"regime\": \"%s\",\n"
                  "    \"targetFps\": %.1f,\n"
                  "    \"emaFrameMs\": %.3f,\n"
                  "    \"renderScale\": %.4f,\n"
                  "    \"volumetricQuality01\": %.4f,\n"
                  "    \"shadowDistanceScale\": %.4f,\n"
                  "    \"floraLodScale\": %.4f,\n"
                  "    \"giRayBudget01\": %.4f\n"
                  "  },\n",
                  static_cast<unsigned>(s.governorEnabled),
                  GovernorRegimeName(static_cast<GovernorRegime>(s.governorRegime)),
                  double(SafeF(s.targetFps, 60.0f)),
                  double(SafeF(s.emaFrameMs, 16.67f)),
                  double(SafeF(s.renderScale, 1.0f)),
                  double(SafeF(s.volumetricQuality01, 1.0f)),
                  double(SafeF(s.shadowDistanceScale, 1.0f)),
                  double(SafeF(s.floraLodScale, 1.0f)),
                  double(SafeF(s.giRayBudget01, 1.0f)));
    os << buf;

    std::snprintf(buf, sizeof(buf),
                  "  \"presets\": {\n"
                  "    \"active\": \"%s\",\n"
                  "    \"recommended\": \"%s\",\n"
                  "    \"overallPressure\": \"%s\",\n"
                  "    \"fallbackRecommended\": %u,\n"
                  "    \"ramUtilizationPct\": %.2f,\n"
                  "    \"vramUtilizationPct\": %.2f,\n"
                  "    \"vramUsedMB\": %.1f,\n"
                  "    \"vramBudgetMB\": %.1f\n"
                  "  },\n",
                  QualityPresetName(static_cast<QualityPreset>(s.activePreset)),
                  QualityPresetName(static_cast<QualityPreset>(s.recommendedPreset)),
                  BudgetPressureName(static_cast<BudgetPressureLevel>(s.overallPressure)),
                  static_cast<unsigned>(s.fallbackRecommended),
                  double(SafeF(s.ramUtilizationPct, 0.0f)),
                  double(SafeF(s.vramUtilizationPct, 0.0f)),
                  double(SafeF(s.vramUsedMB, 0.0f)),
                  double(SafeF(s.vramBudgetMB, 6144.0f)));
    os << buf;

    const std::string dirtyEsc = EscapeJson(s.lastDirtyShader[0] ? s.lastDirtyShader : "none");
    std::snprintf(buf, sizeof(buf),
                  "  \"shaderHotReload\": {\n"
                  "    \"watcherActive\": %u,\n"
                  "    \"reloadGeneration\": %u,\n"
                  "    \"trackedFiles\": %u,\n"
                  "    \"dirtyFiles\": %u,\n"
                  "    \"acceptedReloads\": %u,\n"
                  "    \"rejectedEdits\": %u,\n"
                  "    \"lastDirtyShader\": \"%s\"\n"
                  "  },\n",
                  static_cast<unsigned>(s.shaderWatcherActive),
                  s.shaderReloadGen, s.shaderTrackedCount, s.shaderDirtyCount,
                  s.shaderAcceptedCount, s.shaderRejectedCount, dirtyEsc.c_str());
    os << buf;

    std::snprintf(buf, sizeof(buf),
                  "  \"audio\": {\n"
                  "    \"mood\": \"%s\",\n"
                  "    \"lpfCutoffHz\": %.1f,\n"
                  "    \"reverbWetMult\": %.3f,\n"
                  "    \"duckGain\": %.3f,\n"
                  "    \"transitions\": %u\n"
                  "  },\n",
                  AudioMoodName(static_cast<AudioMood>(s.audioMood)),
                  double(SafeF(s.audioLpfCutoffHz, 18000.0f)),
                  double(SafeF(s.audioReverbWetMult, 0.85f)),
                  double(SafeF(s.audioDuckGain, 1.0f)),
                  s.audioTransitions);
    os << buf;

    std::snprintf(buf, sizeof(buf),
                  "  \"worldAndMods\": {\n"
                  "    \"surfaceTempC\": %.2f,\n"
                  "    \"snowpackM\": %.4f,\n"
                  "    \"windBeaufort\": %.2f,\n"
                  "    \"activeHandles\": %u,\n"
                  "    \"saveMaxStrings\": %u,\n"
                  "    \"modPluginCount\": %u,\n"
                  "    \"modIssueCount\": %u\n"
                  "  }\n",
                  double(SafeF(s.surfaceTempC, 12.0f)),
                  double(SafeF(s.snowpackM, 0.0f)),
                  double(SafeF(s.windBeaufort, 2.0f)),
                  s.activeHandles, s.saveMaxStrings, s.modPluginCount, s.modIssueCount);
    os << buf;
    os << "}\n";
    return os.str();
}

std::string FormatLiveDashboardHtml(const LiveDashboardSnapshot& s) {
    std::ostringstream os;
    char buf[512];

    const char* gradeStr    = HealthGradeName(static_cast<HealthGrade>(s.healthGrade));
    const char* govStr      = GovernorRegimeName(static_cast<GovernorRegime>(s.governorRegime));
    const char* presetStr   = QualityPresetName(static_cast<QualityPreset>(s.activePreset));
    const char* recStr      = QualityPresetName(static_cast<QualityPreset>(s.recommendedPreset));
    const char* pressureStr = BudgetPressureName(static_cast<BudgetPressureLevel>(s.overallPressure));
    const char* moodStr     = AudioMoodName(static_cast<AudioMood>(s.audioMood));
    const uint32_t subCount = PopCount32(s.activeSubsystemMask);
    const char* scoreColor  = (s.healthScore >= 85u) ? "#34d399" :
                              (s.healthScore >= 70u) ? "#fbbf24" : "#f87171";

    os << "<!DOCTYPE html>\n"
       << "<html lang=\"en\">\n"
       << "<head>\n"
       << "<meta charset=\"utf-8\">\n"
       << "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">\n"
       << "<title>RAW Engine Live Telemetry Dashboard</title>\n"
       << "<style>\n"
       << "body{margin:0;padding:24px;background:#0b1020;color:#e5e7eb;font-family:Segoe UI,Consolas,monospace;}\n"
       << "h1{margin:0 0 6px 0;font-size:22px;color:#f8fafc;}\n"
       << ".sub{color:#94a3b8;font-size:13px;margin-bottom:20px;}\n"
       << ".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(290px,1fr));gap:16px;}\n"
       << ".card{background:#111827;border:1px solid #1f2937;border-radius:10px;padding:16px;}\n"
       << ".card h2{margin:0 0 10px 0;font-size:14px;text-transform:uppercase;letter-spacing:0.06em;color:#60a5fa;}\n"
       << ".metric{display:flex;justify-content:space-between;padding:4px 0;border-bottom:1px solid #1f2937;font-size:13px;}\n"
       << ".metric:last-child{border-bottom:none;}\n"
       << ".k{color:#9ca3af;}.v{color:#f3f4f6;font-weight:600;}\n"
       << ".bar{height:8px;background:#1f2937;border-radius:4px;overflow:hidden;margin:8px 0 12px 0;}\n"
       << ".fill{height:100%;border-radius:4px;}\n"
       << ".adv{margin-top:8px;padding:8px;background:#1e1b2e;border-left:3px solid #f59e0b;font-size:12px;color:#fde68a;}\n"
       << "</style>\n"
       << "</head>\n"
       << "<body>\n"
       << "<h1>RAW Engine Runtime Live Telemetry Dashboard</h1>\n";

    std::snprintf(buf, sizeof(buf),
                  "<div class=\"sub\">Snapshot #%u | SDK v1.0.0 (0x%08X) | Active Subsystems: %u/17 (0x%05X) | Verified GMST Modules: %u</div>\n",
                  s.exportNumber, s.sdkVersion, subCount, s.activeSubsystemMask, s.verifiedGmstModules);
    os << buf << "<div class=\"grid\">\n";

    // Card 1: Engine Health
    os << "<section class=\"card\"><h2>Engine Health Scoreboard</h2>\n";
    std::snprintf(buf, sizeof(buf),
                  "<div class=\"metric\"><span class=\"k\">Composite Score</span><span class=\"v\" style=\"color:%s\">%u / 100 (%s)</span></div>\n"
                  "<div class=\"bar\"><div class=\"fill\" style=\"width:%u%%;background:%s\"></div></div>\n"
                  "<div class=\"metric\"><span class=\"k\">Binary / Handles</span><span class=\"v\">%u/15 | %u/20</span></div>\n"
                  "<div class=\"metric\"><span class=\"k\">Saves / Mods</span><span class=\"v\">%u/20 | %u/20</span></div>\n"
                  "<div class=\"metric\"><span class=\"k\">Pacing / Diagnostics</span><span class=\"v\">%u/17 | %u/8</span></div>\n",
                  scoreColor, static_cast<unsigned>(s.healthScore), gradeStr,
                  static_cast<unsigned>(std::min<uint8_t>(s.healthScore, 100u)), scoreColor,
                  static_cast<unsigned>(s.pillarBinary), static_cast<unsigned>(s.pillarHandles),
                  static_cast<unsigned>(s.pillarSaves), static_cast<unsigned>(s.pillarMods),
                  static_cast<unsigned>(s.pillarPacing), static_cast<unsigned>(s.pillarDiag));
    os << buf;
    for (uint8_t i = 0u; i < s.advisoryCount && i < 4u; ++i) {
        os << "<div class=\"adv\">" << EscapeHtml(s.advisories[i]) << "</div>\n";
    }
    os << "</section>\n";

    // Card 2: Frame Pacing & Flight Recorder
    os << "<section class=\"card\"><h2>Frame Pacing &amp; Flight Recorder</h2>\n";
    std::snprintf(buf, sizeof(buf),
                  "<div class=\"metric\"><span class=\"k\">Recorded Frames</span><span class=\"v\">%llu (ring %u/300)</span></div>\n"
                  "<div class=\"metric\"><span class=\"k\">Mean / P50</span><span class=\"v\">%.2f ms / %.2f ms</span></div>\n"
                  "<div class=\"metric\"><span class=\"k\">P95 / P99</span><span class=\"v\">%.2f ms / %.2f ms</span></div>\n"
                  "<div class=\"metric\"><span class=\"k\">Max / Hitches (&gt;50ms)</span><span class=\"v\">%.2f ms / %u</span></div>\n",
                  static_cast<unsigned long long>(s.totalRecordedFrames), s.ringFillCount,
                  double(SafeF(s.meanFrameMs, 16.67f)), double(SafeF(s.p50FrameMs, 16.67f)),
                  double(SafeF(s.p95FrameMs, 16.67f)), double(SafeF(s.p99FrameMs, 16.67f)),
                  double(SafeF(s.maxFrameMs, 16.67f)), s.hitchFramesOver50ms);
    os << buf << "</section>\n";

    // Card 3: PI Frame Governor
    os << "<section class=\"card\"><h2>PI Quality Governor</h2>\n";
    std::snprintf(buf, sizeof(buf),
                  "<div class=\"metric\"><span class=\"k\">Regime / Target</span><span class=\"v\">%s (%.0f FPS)</span></div>\n"
                  "<div class=\"metric\"><span class=\"k\">EMA Frame Time</span><span class=\"v\">%.2f ms</span></div>\n"
                  "<div class=\"metric\"><span class=\"k\">Render / Volumetrics</span><span class=\"v\">%.2fx / %.2fx</span></div>\n"
                  "<div class=\"metric\"><span class=\"k\">Shadows / Flora / GI</span><span class=\"v\">%.2f / %.2f / %.2f</span></div>\n",
                  govStr, double(SafeF(s.targetFps, 60.0f)),
                  double(SafeF(s.emaFrameMs, 16.67f)),
                  double(SafeF(s.renderScale, 1.0f)), double(SafeF(s.volumetricQuality01, 1.0f)),
                  double(SafeF(s.shadowDistanceScale, 1.0f)),
                  double(SafeF(s.floraLodScale, 1.0f)),
                  double(SafeF(s.giRayBudget01, 1.0f)));
    os << buf << "</section>\n";

    // Card 4: Presets & Memory Budget Watchdog
    os << "<section class=\"card\"><h2>Presets &amp; VRAM Watchdog</h2>\n";
    std::snprintf(buf, sizeof(buf),
                  "<div class=\"metric\"><span class=\"k\">Active / Recommended</span><span class=\"v\">%s / %s</span></div>\n"
                  "<div class=\"metric\"><span class=\"k\">Overall Pressure</span><span class=\"v\">%s (fallback=%u)</span></div>\n"
                  "<div class=\"metric\"><span class=\"k\">Host RAM Usage</span><span class=\"v\">%.1f%%</span></div>\n"
                  "<div class=\"metric\"><span class=\"k\">GPU VRAM Usage</span><span class=\"v\">%.0f / %.0f MB (%.1f%%)</span></div>\n",
                  presetStr, recStr, pressureStr, static_cast<unsigned>(s.fallbackRecommended),
                  double(SafeF(s.ramUtilizationPct, 0.0f)),
                  double(SafeF(s.vramUsedMB, 0.0f)), double(SafeF(s.vramBudgetMB, 6144.0f)),
                  double(SafeF(s.vramUtilizationPct, 0.0f)));
    os << buf << "</section>\n";

    // Card 5: Shader Hot-Reload Watchdog
    const std::string dirtyHtml = EscapeHtml(s.lastDirtyShader[0] ? s.lastDirtyShader : "none");
    os << "<section class=\"card\"><h2>Shader Hot-Reload Watchdog</h2>\n";
    std::snprintf(buf, sizeof(buf),
                  "<div class=\"metric\"><span class=\"k\">Watcher / Generation</span><span class=\"v\">active=%u | gen=%u</span></div>\n"
                  "<div class=\"metric\"><span class=\"k\">Tracked / Dirty</span><span class=\"v\">%u / %u</span></div>\n"
                  "<div class=\"metric\"><span class=\"k\">Accepted / Rejected</span><span class=\"v\">%u / %u</span></div>\n"
                  "<div class=\"metric\"><span class=\"k\">Last Dirty Shader</span><span class=\"v\">%s</span></div>\n",
                  static_cast<unsigned>(s.shaderWatcherActive), s.shaderReloadGen,
                  s.shaderTrackedCount, s.shaderDirtyCount,
                  s.shaderAcceptedCount, s.shaderRejectedCount, dirtyHtml.c_str());
    os << buf << "</section>\n";

    // Card 6: Adaptive Audio & World/Mods
    os << "<section class=\"card\"><h2>Adaptive Audio &amp; World State</h2>\n";
    std::snprintf(buf, sizeof(buf),
                  "<div class=\"metric\"><span class=\"k\">Audio Mood</span><span class=\"v\">%s (sw=%u)</span></div>\n"
                  "<div class=\"metric\"><span class=\"k\">LPF / Reverb / Duck</span><span class=\"v\">%.0f Hz / %.2fx / %.2f</span></div>\n"
                  "<div class=\"metric\"><span class=\"k\">Weather</span><span class=\"v\">%.1f C | snow %.2f m | Bft %.1f</span></div>\n"
                  "<div class=\"metric\"><span class=\"k\">Handles / Saves / Mods</span><span class=\"v\">%u | %u str | %u (%u err)</span></div>\n",
                  moodStr, s.audioTransitions,
                  double(SafeF(s.audioLpfCutoffHz, 18000.0f)),
                  double(SafeF(s.audioReverbWetMult, 0.85f)),
                  double(SafeF(s.audioDuckGain, 1.0f)),
                  double(SafeF(s.surfaceTempC, 12.0f)), double(SafeF(s.snowpackM, 0.0f)),
                  double(SafeF(s.windBeaufort, 2.0f)),
                  s.activeHandles, s.saveMaxStrings, s.modPluginCount, s.modIssueCount);
    os << buf << "</section>\n";

    os << "</div>\n</body>\n</html>\n";
    return os.str();
}

std::string LiveDashboardInit(const std::string& outDir) {
    {
        std::lock_guard<std::mutex> lk(g_ld.mu);
        g_ld.initialized             = true;
        g_ld.outDir                  = outDir;
        g_ld.presentTick             = 0u;
        g_ld.state                   = {};
        g_ld.state.dashboardActive   = 1u;
        g_ld.state.autoCadenceFrames = kLiveDashboardCadenceFrames;
        g_ld.state.sdkVersion        = RAW_GetSdkVersion();
    }
    char buf[224];
    std::snprintf(buf, sizeof(buf),
                  "[LiveDashboard] initialized (cadence=%uf -> %s\\raw_live_dashboard.{json,html})",
                  kLiveDashboardCadenceFrames, outDir.empty() ? "raw_engine_out" : outDir.c_str());
    return buf;
}

bool LiveDashboardExportNow(const char* overrideOutDir, std::string* outSummary) {
    std::string targetDir;
    uint32_t nextExportNum = 1u;
    {
        std::lock_guard<std::mutex> lk(g_ld.mu);
        if (overrideOutDir && overrideOutDir[0] != '\0') {
            targetDir = overrideOutDir;
        } else {
            targetDir = g_ld.outDir;
        }
        nextExportNum = g_ld.state.exportCount + 1u;
    }
    if (targetDir.empty()) {
        if (outSummary) {
            *outSummary = "[LiveDashboard] export skipped (no output directory configured)";
        }
        return false;
    }

    const LiveDashboardSnapshot snap = CaptureLiveDashboardSnapshot(nextExportNum);
    const std::string jsonStr = FormatLiveDashboardJson(snap);
    const std::string htmlStr = FormatLiveDashboardHtml(snap);

    const std::string jsonPath = (std::filesystem::path(targetDir) / "raw_live_dashboard.json").string();
    const std::string htmlPath = (std::filesystem::path(targetDir) / "raw_live_dashboard.html").string();

    const bool okJson = AtomicWriteStringFile(jsonPath, jsonStr);
    const bool okHtml = AtomicWriteStringFile(htmlPath, htmlStr);
    const bool okAll  = okJson && okHtml;

    RawLiveDashboardState stCopy{};
    {
        std::lock_guard<std::mutex> lk(g_ld.mu);
        g_ld.state.dashboardActive        = g_ld.initialized ? 1u : 0u;
        g_ld.state.lastExportOk           = okAll ? 1u : 0u;
        g_ld.state.healthScore            = snap.healthScore;
        g_ld.state.overallPressure        = snap.overallPressure;
        if (okAll) {
            g_ld.state.exportCount = nextExportNum;
        }
        g_ld.state.lastJsonBytes          = static_cast<uint32_t>(jsonStr.size());
        g_ld.state.lastHtmlBytes          = static_cast<uint32_t>(htmlStr.size());
        g_ld.state.sdkVersion             = snap.sdkVersion;
        g_ld.state.activeSubsystemMask    = snap.activeSubsystemMask;
        g_ld.state.shaderReloadGeneration = snap.shaderReloadGen;
        CopyTrunc(g_ld.state.lastJsonPath, sizeof(g_ld.state.lastJsonPath), jsonPath);
        CopyTrunc(g_ld.state.lastHtmlPath, sizeof(g_ld.state.lastHtmlPath), htmlPath);
        stCopy = g_ld.state;
    }

    if (outSummary) {
        char buf[320];
        std::snprintf(buf, sizeof(buf),
                      "[LiveDashboard] exported=%s count=%u json=%uB html=%uB health=%u/100 subsys=%u/17 gen=%u",
                      okAll ? "yes" : "no", stCopy.exportCount,
                      stCopy.lastJsonBytes, stCopy.lastHtmlBytes,
                      static_cast<unsigned>(stCopy.healthScore),
                      PopCount32(stCopy.activeSubsystemMask),
                      stCopy.shaderReloadGeneration);
        *outSummary = buf;
    }
    return okAll;
}

void LiveDashboardOnPresent() {
    bool shouldExport = false;
    {
        std::lock_guard<std::mutex> lk(g_ld.mu);
        if (!g_ld.initialized || g_ld.outDir.empty()) return;
        ++g_ld.presentTick;
        if ((g_ld.presentTick % kLiveDashboardCadenceFrames) == 0u) {
            shouldExport = true;
        }
    }
    if (shouldExport) {
        (void)LiveDashboardExportNow(nullptr, nullptr);
    }
}

void LiveDashboardShutdown() {
    std::string finalDir;
    {
        std::lock_guard<std::mutex> lk(g_ld.mu);
        if (g_ld.initialized && !g_ld.outDir.empty()) {
            finalDir = g_ld.outDir;
        }
        g_ld.initialized           = false;
        g_ld.presentTick           = 0u;
        g_ld.state.dashboardActive = 0u;
        g_ld.outDir.clear();
    }
    if (!finalDir.empty()) {
        (void)LiveDashboardExportNow(finalDir.c_str(), nullptr);
    }
}

}  // namespace raw::rt

extern "C" __declspec(dllexport) int RAW_GetLiveDashboardState(raw::rt::RawLiveDashboardState* out) {
    if (!out) return 0;
    std::lock_guard<std::mutex> lk(raw::rt::g_ld.mu);
    *out = raw::rt::g_ld.state;
    return raw::rt::g_ld.initialized ? 1 : 0;
}
