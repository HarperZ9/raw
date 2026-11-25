// rt_live_config.cpp: Wave 44 Live Hot-Reloading Configuration (raw_engine.ini) & F10 In-Game Runtime Diagnostics Dump.
// Original work, MIT.
#include "rt_live_config.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#ifndef RAW_SIM_PURE_TEST
#include "rt_combat_ai.h"
#include "rt_combat_impact.h"
#include "rt_economy_ledger.h"
#include "rt_movement_combat.h"
#include "rt_save_papyrus_guard.h"
#include "rt_softbody.h"
#include "rt_stealth_survival.h"
#include "rt_version.h"
#include "rt_view.h"
#endif

namespace raw::rt {
namespace {

std::mutex       g_cfgMu;
LiveEngineConfig g_activeCfg = DefaultLiveEngineConfig();

std::string Trim(std::string s) {
    size_t b = 0;
    while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    size_t e = s.size();
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

std::string ToLower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool ParseBoolVal(const std::string& raw, bool& out) {
    const std::string v = ToLower(Trim(raw));
    if (v == "1" || v == "true" || v == "on" || v == "yes")  { out = true;  return true; }
    if (v == "0" || v == "false" || v == "off" || v == "no") { out = false; return true; }
    return false;
}

bool ParseFloatVal(const std::string& raw, float& out) {
    const std::string v = Trim(raw);
    if (v.empty()) return false;
    char* endPtr = nullptr;
    const double d = std::strtod(v.c_str(), &endPtr);
    if (endPtr == v.c_str() || !std::isfinite(d)) return false;
    while (*endPtr && std::isspace(static_cast<unsigned char>(*endPtr))) ++endPtr;
    if (*endPtr != '\0') return false;
    out = static_cast<float>(d);
    return std::isfinite(out);
}

bool ParseUintVal(const std::string& raw, uint32_t& out) {
    const std::string v = Trim(raw);
    if (v.empty() || v[0] == '-') return false;
    char* endPtr = nullptr;
    const unsigned long u = std::strtoul(v.c_str(), &endPtr, 0);
    if (endPtr == v.c_str()) return false;
    while (*endPtr && std::isspace(static_cast<unsigned char>(*endPtr))) ++endPtr;
    if (*endPtr != '\0') return false;
    out = static_cast<uint32_t>(std::min<unsigned long>(u, 0xFFFFFFFFul));
    return true;
}

float ClampFinite(float v, float lo, float hi, float defVal) {
    if (!std::isfinite(v)) return defVal;
    return std::clamp(v, lo, hi);
}

bool AtomicWriteStringFile(const std::string& filePath, const std::string& payload) {
    if (filePath.empty()) return false;
    try {
        const std::filesystem::path target(filePath);
        if (target.has_parent_path()) {
            std::error_code ec;
            std::filesystem::create_directories(target.parent_path(), ec);
        }
        const std::string tmpPath = filePath + ".tmp";
        {
            std::ofstream out(tmpPath, std::ios::binary | std::ios::trunc);
            if (!out) return false;
            out.write(payload.data(), static_cast<std::streamsize>(payload.size()));
            out.flush();
            if (!out.good()) return false;
        }
        if (MoveFileExA(tmpPath.c_str(), filePath.c_str(),
                        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            return true;
        }
        std::error_code ec;
        std::filesystem::rename(tmpPath, target, ec);
        if (ec) {
            std::filesystem::copy_file(tmpPath, target, std::filesystem::copy_options::overwrite_existing, ec);
            std::error_code ecRm;
            std::filesystem::remove(tmpPath, ecRm);
            return !ec;
        }
        return true;
    } catch (...) {
        return false;
    }
}

}  // namespace

LiveEngineConfig DefaultLiveEngineConfig() {
    return LiveEngineConfig{};
}

void ClampLiveEngineConfig(LiveEngineConfig& cfg) {
    const LiveEngineConfig def{};
    cfg.parryWindowMs          = ClampFinite(cfg.parryWindowMs,    60.0f, 450.0f, def.parryWindowMs);
    cfg.dodgeIFrameMs          = ClampFinite(cfg.dodgeIFrameMs,    80.0f, 500.0f, def.dodgeIFrameMs);
    cfg.dodgeStaminaCost       = ClampFinite(cfg.dodgeStaminaCost,  0.0f,  60.0f, def.dodgeStaminaCost);
    cfg.hitstopScale           = ClampFinite(cfg.hitstopScale,      0.0f,   2.0f, def.hitstopScale);
    cfg.xpbdWindScale          = ClampFinite(cfg.xpbdWindScale,     0.0f,   3.0f, def.xpbdWindScale);
    cfg.fovBreathScale         = ClampFinite(cfg.fovBreathScale,    0.0f,   2.5f, def.fovBreathScale);
    cfg.liveAudioGain          = ClampFinite(cfg.liveAudioGain,     0.0f,   0.50f, def.liveAudioGain);
    if (cfg.statusDumpHotkeyVK != 0u) {
        cfg.statusDumpHotkeyVK = std::clamp(cfg.statusDumpHotkeyVK, 0x08u, 0xFEu);
    }
    cfg.hotReloadIntervalFrames = std::clamp(cfg.hotReloadIntervalFrames, 10u, 600u);
}

std::string FormatDefaultLiveConfigIni(const LiveEngineConfig& inCfg) {
    LiveEngineConfig c = inCfg;
    ClampLiveEngineConfig(c);
    std::ostringstream os;
    char fbuf[32];
    auto fmtF = [&](float v, int prec = 1) -> const char* {
        std::snprintf(fbuf, sizeof fbuf, prec == 0 ? "%.0f" : (prec == 2 ? "%.2f" : "%.1f"), v);
        return fbuf;
    };

    os << "; RAW Engine Runtime Live Configuration (hot-reloaded automatically while playing!)\n"
       << "; Edit any value and save: changes apply within HotReloadIntervalFrames without restarting Skyrim.\n\n"
       << "[Combat]\n"
       << "; Precision timed parry guard window in milliseconds (60..450, default 220)\n"
       << "ParryWindowMs=" << fmtF(c.parryWindowMs, 0) << "\n"
       << "; Directional dodge-step invulnerability duration in milliseconds (80..500, default 260)\n"
       << "DodgeIFrameMs=" << fmtF(c.dodgeIFrameMs, 0) << "\n"
       << "; Stamina deducted per dodge-step (0..60, default 18)\n"
       << "DodgeStaminaCost=" << fmtF(c.dodgeStaminaCost, 0) << "\n"
       << "; Enable/disable 260ms dodge-step i-frame hit evasion (0/1)\n"
       << "DodgeIFramesEnabled=" << (c.dodgeIFramesEnabled ? 1 : 0) << "\n"
       << "; Enable/disable tactile melee hitstop on parry, guard-break & lethal kills (0/1)\n"
       << "HitstopEnabled=" << (c.hitstopEnabled ? 1 : 0) << "\n"
       << "; Multiplier for hitstop intensity (0.0..2.0, default 1.0)\n"
       << "HitstopScale=" << fmtF(c.hitstopScale, 1) << "\n"
       << "; Enable/disable non-spongy lethal difficulty tiers (0/1)\n"
       << "NonSpongyDifficulty=" << (c.nonSpongyDifficulty ? 1 : 0) << "\n"
       << "; Enable/disable tactical flanking squad AI combat chances (0/1)\n"
       << "TacticalSquadAI=" << (c.tacticalSquadAi ? 1 : 0) << "\n\n"
       << "[Physics]\n"
       << "; Enable/disable live post-Havok skeleton IK + 240Hz XPBD cloth/hair solver (0/1)\n"
       << "SkeletonXpbdEnabled=" << (c.skeletonXpbdEnabled ? 1 : 0) << "\n"
       << "; Wind force multiplier for XPBD cloth & hair (0.0..3.0, default 1.0)\n"
       << "XpbdWindScale=" << fmtF(c.xpbdWindScale, 1) << "\n"
       << "; Enable/disable smooth critically-damped sprint/combat camera FOV breath (0/1)\n"
       << "FovBreathEnabled=" << (c.fovBreathEnabled ? 1 : 0) << "\n"
       << "; Scale factor for sprint/combat camera FOV breath (0.0..2.5, default 1.0)\n"
       << "FovBreathScale=" << fmtF(c.fovBreathScale, 1) << "\n\n"
       << "[World]\n"
       << "; Enable/disable tfc free-camera LOD4 terrain void-guard (0/1)\n"
       << "TfcLodVoidGuard=" << (c.tfcLodVoidGuard ? 1 : 0) << "\n"
       << "; Enable/disable 9-Hold persistent living Skyrim economy & barter pricing (0/1)\n"
       << "LivingEconomyEnabled=" << (c.livingEconomyEnabled ? 1 : 0) << "\n"
       << "; Enable/disable weather-masked acoustic sneak detection (0/1)\n"
       << "WeatherStealthEnabled=" << (c.weatherStealthEnabled ? 1 : 0) << "\n"
       << "; Enable/disable thermal exposure & hypothermia stamina survival (0/1)\n"
       << "ThermalSurvivalEnabled=" << (c.thermalSurvivalEnabled ? 1 : 0) << "\n\n"
       << "[Audio]\n"
       << "; Opt-in live 48kHz WASAPI FDN-8 ambient weather/fire/water soundscape (0/1, default 0)\n"
       << "LiveAudioAmbience=" << (c.liveAudioAmbience ? 1 : 0) << "\n"
       << "; Master gain for live procedural ambience (0.00..0.50, default 0.22)\n"
       << "LiveAudioGain=" << fmtF(c.liveAudioGain, 2) << "\n\n"
       << "[Stability]\n"
       << "; Enable/disable .ESS save-file health auditor & Papyrus frame-hitch watchdog (0/1, default 1)\n"
       << "SavePapyrusGuard=" << (c.savePapyrusGuardEnabled ? 1 : 0) << "\n\n"
       << "[Diagnostics]\n"
       << "; Virtual-key code for live status dump (default 121 = VK_F10; 0 disables)\n"
       << "StatusDumpHotkeyVK=" << c.statusDumpHotkeyVK << "\n"
       << "; Frame cadence for live raw_engine.ini mtime hot-reload check (10..600, default 60)\n"
       << "HotReloadIntervalFrames=" << c.hotReloadIntervalFrames << "\n"
       << "; Enable/disable VEH fatal crash forensics & 7,376-class RTTI symbolizer (0/1, default 1)\n"
       << "CrashForensics=" << (c.crashForensicsEnabled ? 1 : 0) << "\n";
    return os.str();
}

bool ParseLiveConfigIni(const std::string& iniText, LiveEngineConfig& outCfg) {
    LiveEngineConfig cfg = DefaultLiveEngineConfig();
    std::istringstream is(iniText);
    std::string line;
    std::string section;

    while (std::getline(is, line)) {
        // Strip UTF-8 BOM if present on first line
        if (line.size() >= 3u &&
            static_cast<unsigned char>(line[0]) == 0xEFu &&
            static_cast<unsigned char>(line[1]) == 0xBBu &&
            static_cast<unsigned char>(line[2]) == 0xBFu) {
            line.erase(0, 3u);
        }
        // Strip inline comments starting with ';' or '#'
        const size_t semi = line.find(';');
        const size_t hash = line.find('#');
        const size_t cut  = std::min(semi, hash);
        if (cut != std::string::npos) line.resize(cut);

        line = Trim(line);
        if (line.empty()) continue;

        if (line.front() == '[' && line.back() == ']' && line.size() >= 3u) {
            section = ToLower(Trim(line.substr(1u, line.size() - 2u)));
            continue;
        }
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;

        const std::string key = ToLower(Trim(line.substr(0, eq)));
        const std::string val = Trim(line.substr(eq + 1u));
        if (key.empty() || val.empty()) continue;

        float f = 0.0f;
        bool b = false;
        uint32_t u = 0u;

        if (section.empty() || section == "combat") {
            if (key == "parrywindowms" && ParseFloatVal(val, f))          cfg.parryWindowMs = f;
            else if (key == "dodgeiframems" && ParseFloatVal(val, f))     cfg.dodgeIFrameMs = f;
            else if (key == "dodgestaminacost" && ParseFloatVal(val, f))  cfg.dodgeStaminaCost = f;
            else if (key == "dodgeiframesenabled" && ParseBoolVal(val, b)) cfg.dodgeIFramesEnabled = b;
            else if (key == "hitstopenabled" && ParseBoolVal(val, b))     cfg.hitstopEnabled = b;
            else if (key == "hitstopscale" && ParseFloatVal(val, f))      cfg.hitstopScale = f;
            else if (key == "nonspongydifficulty" && ParseBoolVal(val, b)) cfg.nonSpongyDifficulty = b;
            else if (key == "tacticalsquadai" && ParseBoolVal(val, b))    cfg.tacticalSquadAi = b;
        }
        if (section.empty() || section == "physics") {
            if (key == "skeletonxpbdenabled" && ParseBoolVal(val, b))     cfg.skeletonXpbdEnabled = b;
            else if (key == "xpbdwindscale" && ParseFloatVal(val, f))     cfg.xpbdWindScale = f;
            else if (key == "fovbreathenabled" && ParseBoolVal(val, b))   cfg.fovBreathEnabled = b;
            else if (key == "fovbreathscale" && ParseFloatVal(val, f))    cfg.fovBreathScale = f;
        }
        if (section.empty() || section == "world") {
            if (key == "tfclodvoidguard" && ParseBoolVal(val, b))         cfg.tfcLodVoidGuard = b;
            else if (key == "livingeconomyenabled" && ParseBoolVal(val, b)) cfg.livingEconomyEnabled = b;
            else if (key == "weatherstealthenabled" && ParseBoolVal(val, b)) cfg.weatherStealthEnabled = b;
            else if (key == "thermalsurvivalenabled" && ParseBoolVal(val, b)) cfg.thermalSurvivalEnabled = b;
        }
        if (section.empty() || section == "audio") {
            if (key == "liveaudioambience" && ParseBoolVal(val, b))       cfg.liveAudioAmbience = b;
            else if (key == "liveaudiogain" && ParseFloatVal(val, f))     cfg.liveAudioGain = f;
        }
        if (section.empty() || section == "stability") {
            if (key == "savepapyrusguard" && ParseBoolVal(val, b))        cfg.savePapyrusGuardEnabled = b;
        }
        if (section.empty() || section == "diagnostics") {
            if (key == "statusdumphotkeyvk" && ParseUintVal(val, u))      cfg.statusDumpHotkeyVK = u;
            else if (key == "hotreloadintervalframes" && ParseUintVal(val, u)) cfg.hotReloadIntervalFrames = u;
            else if (key == "crashforensics" && ParseBoolVal(val, b))     cfg.crashForensicsEnabled = b;
        }
    }

    ClampLiveEngineConfig(cfg);
    outCfg = cfg;
    return true;
}

bool EnsureDefaultLiveConfigFile(const std::string& iniPath, const LiveEngineConfig& defaults) {
    if (iniPath.empty()) return false;
    std::error_code ec;
    if (std::filesystem::exists(iniPath, ec)) {
        return true;
    }
    return AtomicWriteStringFile(iniPath, FormatDefaultLiveConfigIni(defaults));
}

bool LoadLiveConfigFromFile(const std::string& iniPath, LiveEngineConfig& outCfg) {
    if (iniPath.empty()) return false;
    std::ifstream in(iniPath, std::ios::binary);
    if (!in) return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    return ParseLiveConfigIni(ss.str(), outCfg);
}

bool LiveConfigHotReloader::QueryFileStamp(uint64_t& outWriteStamp, uint64_t& outSize) const {
    if (iniPath_.empty()) return false;
    WIN32_FILE_ATTRIBUTE_DATA fad{};
    if (!GetFileAttributesExA(iniPath_.c_str(), GetFileExInfoStandard, &fad)) {
        return false;
    }
    outWriteStamp = (static_cast<uint64_t>(fad.ftLastWriteTime.dwHighDateTime) << 32) |
                    static_cast<uint64_t>(fad.ftLastWriteTime.dwLowDateTime);
    outSize = (static_cast<uint64_t>(fad.nFileSizeHigh) << 32) |
              static_cast<uint64_t>(fad.nFileSizeLow);
    return true;
}

bool LiveConfigHotReloader::Init(const std::string& iniPath, bool autoCreateDefault) {
    iniPath_        = iniPath;
    cfg_            = DefaultLiveEngineConfig();
    frameCounter_   = 0;
    reloadCount_    = 0;
    lastWriteStamp_ = 0;
    lastFileSize_   = 0;

    if (autoCreateDefault && !iniPath_.empty()) {
        EnsureDefaultLiveConfigFile(iniPath_, cfg_);
    }
    LoadLiveConfigFromFile(iniPath_, cfg_);
    QueryFileStamp(lastWriteStamp_, lastFileSize_);
    initialized_ = true;
    return true;
}

bool LiveConfigHotReloader::PollReload(bool forceCheck) {
    if (!initialized_ || iniPath_.empty()) return false;
    ++frameCounter_;
    const uint32_t interval = std::max(cfg_.hotReloadIntervalFrames, 10u);
    if (!forceCheck && (frameCounter_ % interval) != 0u) {
        return false;
    }

    uint64_t stamp = 0, sz = 0;
    if (!QueryFileStamp(stamp, sz)) {
        return false;
    }
    if (stamp == lastWriteStamp_ && sz == lastFileSize_) {
        return false;
    }

    LiveEngineConfig nextCfg{};
    if (!LoadLiveConfigFromFile(iniPath_, nextCfg)) {
        return false;
    }
    cfg_            = nextCfg;
    lastWriteStamp_ = stamp;
    lastFileSize_   = sz;
    ++reloadCount_;
    return true;
}

std::string FormatLiveStatusDump(const LiveStatusDumpData& d) {
    std::ostringstream os;
    char buf[256];
    os << "========================================================================\n"
       << "RAW ENGINE RUNTIME - LIVE DIAGNOSTICS STATUS DUMP\n"
       << "========================================================================\n";
    std::snprintf(buf, sizeof buf, "Runtime: SkyrimSE %s | Zydis Binary Verification: %u/%u (%s)\n",
                  d.runtimeVersion.c_str(), d.zydisPassed, d.zydisChecked,
                  d.zydisOk ? "OK - VERIFIED" : "FAIL-CLOSED");
    os << buf;
    std::snprintf(buf, sizeof buf, "Config: %s | HotReloads: %u | HotkeyVK: %u | PollInterval: %u frames | CrashForensics: %u\n\n",
                  d.iniPath.empty() ? "raw_engine.ini" : d.iniPath.c_str(),
                  d.configReloads, d.config.statusDumpHotkeyVK, d.config.hotReloadIntervalFrames,
                  d.config.crashForensicsEnabled ? 1u : 0u);
    os << buf;

    os << "[Active Configuration]\n";
    std::snprintf(buf, sizeof buf,
                  "  Combat  : ParryWindowMs=%.0f DodgeIFrameMs=%.0f DodgeStam=%.0f DodgeIFrames=%u Hitstop=%u(x%.2f) NonSpongy=%u SquadAI=%u\n",
                  d.config.parryWindowMs, d.config.dodgeIFrameMs, d.config.dodgeStaminaCost,
                  d.config.dodgeIFramesEnabled ? 1u : 0u, d.config.hitstopEnabled ? 1u : 0u,
                  d.config.hitstopScale, d.config.nonSpongyDifficulty ? 1u : 0u,
                  d.config.tacticalSquadAi ? 1u : 0u);
    os << buf;
    std::snprintf(buf, sizeof buf,
                  "  Physics : SkeletonXPBD=%u WindScale=%.2f FovBreath=%u FovScale=%.2f\n",
                  d.config.skeletonXpbdEnabled ? 1u : 0u, d.config.xpbdWindScale,
                  d.config.fovBreathEnabled ? 1u : 0u, d.config.fovBreathScale);
    os << buf;
    std::snprintf(buf, sizeof buf,
                  "  World   : TfcLodGuard=%u LivingEconomy=%u WeatherStealth=%u ThermalSurvival=%u\n",
                  d.config.tfcLodVoidGuard ? 1u : 0u, d.config.livingEconomyEnabled ? 1u : 0u,
                  d.config.weatherStealthEnabled ? 1u : 0u, d.config.thermalSurvivalEnabled ? 1u : 0u);
    os << buf;
    std::snprintf(buf, sizeof buf,
                  "  Audio   : LiveAmbience=%u LiveGain=%.2f | Stability: SavePapyrusGuard=%u\n\n",
                  d.config.liveAudioAmbience ? 1u : 0u, d.config.liveAudioGain,
                  d.config.savePapyrusGuardEnabled ? 1u : 0u);
    os << buf;

    os << "[Skeleton IK & 240Hz XPBD Soft-Body]\n";
    std::snprintf(buf, sizeof buf,
                  "  MainDrawHook=%u ActiveActors=%u IkBones=%u XpbdBones=%u UpdatedNodes=%u TotalRuns=%llu\n\n",
                  d.skeletonHookActive ? 1u : 0u, d.skeletonActors, d.skeletonIkBones,
                  d.skeletonXpbdBones, d.skeletonUpdated, static_cast<unsigned long long>(d.skeletonTotalRuns));
    os << buf;

    os << "[Combat, Parry, Dodge i-Frames, Hitstop & Camera FOV]\n";
    std::snprintf(buf, sizeof buf,
                  "  HooksMask=0x%02X Hits=%u TimedParries=%u GuardBreaks=%u CounterHits=%u\n",
                  d.gameplayHooksMask, d.hitsProcessed, d.timedParries, d.guardBreaks, d.counterHits);
    os << buf;
    std::snprintf(buf, sizeof buf,
                  "  Dodges=%u EvadedHits=%u HitstopVerified=%u HitstopTriggers=%u ImpactDecals=%u/%u\n",
                  d.dodgesTriggered, d.evadedHits, d.hitstopVerified ? 1u : 0u, d.hitstopTriggers,
                  d.decalsActiveNow, d.decalsTotal);
    os << buf;
    std::snprintf(buf, sizeof buf,
                  "  CameraFOV: Baseline=%.1f deg | AppliedBreathOffset=+%.2f deg\n\n",
                  d.baselineFovDeg, d.appliedFovOffset);
    os << buf;

    os << "[9-Hold Living Skyrim Economy & NPC Ecosystem]\n";
    std::snprintf(buf, sizeof buf,
                  "  ActiveHold=%s MarketTier=%u DaysSimulated=%u DayOfYear=%u PlayerTrades=%u\n",
                  d.activeHoldName.c_str(), d.marketTier, d.economyDaysSim, d.economyDayOfYear, d.economyTrades);
    os << buf;
    std::snprintf(buf, sizeof buf,
                  "  BuyMult=%.2f SellMult=%.2f Scarcity=%.2f CaravanDisruption=%.2f | NPC Directive=%u Shelter=%.2f Torch=%.2f\n\n",
                  d.economyBuyMult, d.economySellMult, d.economyScarcity01, d.caravanDisruption01,
                  d.npcDirective, d.npcShelterUrgency01, d.npcTorchNeed01);
    os << buf;

    os << "[Weather Stealth, Thermal Survival & 3D Acoustics]\n";
    std::snprintf(buf, sizeof buf,
                  "  SneakHook=%u Checks=%u WeatherMasked=%u | Warmth=%.1f Wetness=%.2f WindChill=%.1fC StamRegenMult=%.2f\n",
                  d.sneakHookActive ? 1u : 0u, d.sneakChecks, d.sneakMasked,
                  d.coreWarmth, d.wetness01, d.windChillC, d.staminaRegenMult);
    os << buf;
    std::snprintf(buf, sizeof buf,
                  "  AcousticEnvClass=%u SabineEyringRT60=%.2fs\n\n",
                  d.acousticEnvKind, d.acousticRt60Sec);
    os << buf;

    os << "[Live Skyrim Calendar]\n";
    std::snprintf(buf, sizeof buf,
                  "  4E %.0f | Month=%.0f Day=%.0f Hour=%.2f | DaysPassed=%.2f | TimeScale=%.1f\n\n",
                  d.calYear, d.calMonth, d.calDay, d.calHour, d.calDaysPassed, d.calTimeScale);
    os << buf;

    os << "[Save-File Health & Papyrus Frame Hitch Watchdog]\n";
    std::snprintf(buf, sizeof buf,
                  "  Guard=%u SavesAudited=%u WorstHealth=%u MaxStrings=%u/65535 MaxSize=%.2fMB | Hitches(>50ms)=%u (60s=%u) Worst=%.1fms P95=%.1fms\n",
                  d.config.savePapyrusGuardEnabled ? 1u : 0u, d.savesAudited, d.worstSaveHealth,
                  d.worstSaveStrings, d.worstSaveSizeMb, d.hitchCount, d.hitchesLast60Sec,
                  d.worstHitchMs, d.p95FrameMs);
    os << buf;
    return os.str();
}

bool WriteLiveStatusDumpToFile(const std::string& outPath, const LiveStatusDumpData& d) {
    return AtomicWriteStringFile(outPath, FormatLiveStatusDump(d));
}

LiveEngineConfig GetLiveConfig() {
    std::lock_guard<std::mutex> lk(g_cfgMu);
    return g_activeCfg;
}

void SetLiveConfigSnapshot(const LiveEngineConfig& cfg) {
    LiveEngineConfig c = cfg;
    ClampLiveEngineConfig(c);
    std::lock_guard<std::mutex> lk(g_cfgMu);
    g_activeCfg = c;
}

#ifndef RAW_SIM_PURE_TEST
namespace {

struct LiveConfigRuntime {
    std::mutex            mu;
    LiveConfigHotReloader reloader;
    std::string           dumpOutPath;
    bool                  initialized = false;
    bool                  prevHotkeyDown = false;
} g_lc;

static bool IsHostWindowForeground() {
    HWND fg = GetForegroundWindow();
    if (!fg) return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);
    return pid == GetCurrentProcessId();
}

static void ParseZydisCountsFromJson(const std::string& json, uint32_t& outPassed, uint32_t& outChecked) {
    auto grab = [&](const char* key, uint32_t& dst) {
        const size_t p = json.find(key);
        if (p == std::string::npos) return;
        const size_t c = json.find(':', p);
        if (c == std::string::npos) return;
        dst = static_cast<uint32_t>(std::strtoul(json.c_str() + c + 1u, nullptr, 10));
    };
    grab("\"passed\"", outPassed);
    grab("\"checked\"", outChecked);
}

}  // namespace

std::string LiveConfigInit(const std::string& iniPath, const std::string& dumpOutPath) {
    LiveEngineConfig snap{};
    bool created = false;
    {
        std::lock_guard<std::mutex> lk(g_lc.mu);
        std::error_code ec;
        const bool existedBefore = !iniPath.empty() && std::filesystem::exists(iniPath, ec);
        g_lc.reloader.Init(iniPath, true);
        created = !existedBefore && !iniPath.empty() && std::filesystem::exists(iniPath, ec);
        g_lc.dumpOutPath    = dumpOutPath;
        g_lc.prevHotkeyDown = false;
        g_lc.initialized    = true;
        snap = g_lc.reloader.Config();
    }
    SetLiveConfigSnapshot(snap);

    char buf[256];
    std::snprintf(buf, sizeof buf,
                  "[LiveConfig] %s %s (parry=%.0fms dodge=%.0fms hitstop=%u econ=%u hotkeyVK=%u poll=%uf)",
                  created ? "generated default" : "loaded",
                  iniPath.c_str(), snap.parryWindowMs, snap.dodgeIFrameMs,
                  snap.hitstopEnabled ? 1u : 0u, snap.livingEconomyEnabled ? 1u : 0u,
                  snap.statusDumpHotkeyVK, snap.hotReloadIntervalFrames);
    return buf;
}

bool LiveConfigPollReload(std::string* logMsgOut) {
    LiveEngineConfig snap{};
    bool reloaded = false;
    uint32_t count = 0;
    std::string path;
    {
        std::lock_guard<std::mutex> lk(g_lc.mu);
        if (!g_lc.initialized) return false;
        reloaded = g_lc.reloader.PollReload(false);
        if (reloaded) {
            snap  = g_lc.reloader.Config();
            count = g_lc.reloader.ReloadCount();
            path  = g_lc.reloader.Path();
        }
    }
    if (!reloaded) return false;

    SetLiveConfigSnapshot(snap);
    if (logMsgOut) {
        char buf[256];
        std::snprintf(buf, sizeof buf,
                      "[LiveConfig] reloaded raw_engine.ini (#%u) parry=%.0fms dodge=%.0fms hitstop=%u(x%.2f) squadAI=%u econ=%u",
                      count, snap.parryWindowMs, snap.dodgeIFrameMs,
                      snap.hitstopEnabled ? 1u : 0u, snap.hitstopScale,
                      snap.tacticalSquadAi ? 1u : 0u, snap.livingEconomyEnabled ? 1u : 0u);
        *logMsgOut = buf;
    }
    return true;
}

bool LiveConfigWriteStatusDump(std::string* outPathWritten) {
    LiveStatusDumpData d{};
    std::string outPath;
    {
        std::lock_guard<std::mutex> lk(g_lc.mu);
        if (!g_lc.initialized) return false;
        d.config        = g_lc.reloader.Config();
        d.configReloads = g_lc.reloader.ReloadCount();
        d.iniPath       = g_lc.reloader.Path();
        outPath         = g_lc.dumpOutPath;
    }

    d.runtimeVersion = TableVersion().empty() ? "1.7.104.0" : TableVersion();
    d.zydisOk        = AreBinaryOffsetsVerified();
    d.zydisPassed    = 0;
    d.zydisChecked   = 0;
    ParseZydisCountsFromJson(BinaryOffsetVerificationJson(), d.zydisPassed, d.zydisChecked);

    const auto sk = GetSkeletonPhysicsStats();
    d.skeletonHookActive = sk.mainDrawHookActive;
    d.skeletonActors     = sk.activeActors;
    d.skeletonIkBones    = sk.lastFrameIkBones;
    d.skeletonXpbdBones  = sk.lastFrameXpbdBones;
    d.skeletonUpdated    = sk.lastFrameUpdated;
    d.skeletonTotalRuns  = sk.totalHooksRun;

    GameplayHookTelemetry gp{};
    if (GameplayHooksGetStats(&gp)) {
        d.gameplayHooksMask = gp.hooksActive;
        d.hitsProcessed     = gp.hitsProcessed;
        d.timedParries      = gp.timedParries;
        d.guardBreaks       = gp.guardBreaks;
        d.counterHits       = gp.counterHits;
    }

    MovementCombatTelemetry mc{};
    if (MovementCombatGetTelemetry(&mc)) {
        d.dodgesTriggered  = mc.dodgesTriggered;
        d.evadedHits       = mc.evadedHits;
        d.baselineFovDeg   = mc.baselineFovDeg;
        d.appliedFovOffset = mc.appliedFovOffset;
    }

    CombatImpactTelemetry ci{};
    if (CombatImpactGetTelemetry(&ci)) {
        d.hitstopVerified = (ci.hitstopVerified != 0u);
        d.hitstopTriggers = ci.hitstopTriggers;
        d.decalsActiveNow = ci.decalsActiveNow;
        d.decalsTotal     = ci.decalsTotal;
    }

    EconomyLedgerTelemetry ec{};
    if (EconomyLedgerGetTelemetry(&ec)) {
        d.activeHoldName      = SkyrimHoldName(static_cast<SkyrimHold>(ec.activeHold));
        d.marketTier          = ec.marketTier;
        d.economyDaysSim      = ec.daysSimulated;
        d.economyDayOfYear    = ec.dayOfYear;
        d.economyTrades       = ec.totalPlayerTrades;
        d.economyBuyMult      = ec.buyPriceMult;
        d.economySellMult     = ec.sellPriceMult;
        d.economyScarcity01   = ec.scarcity01;
        d.caravanDisruption01 = ec.caravanDisruption01;
        d.npcDirective        = ec.npcDirective;
        d.npcShelterUrgency01 = ec.npcShelterUrgency01;
        d.npcTorchNeed01      = ec.npcTorchNeed01;
    }

    StealthSurvivalTelemetry ss{};
    if (StealthSurvivalGetTelemetry(&ss)) {
        d.sneakHookActive  = (ss.sneakHookActive != 0u);
        d.sneakChecks      = ss.sneakChecksTotal;
        d.sneakMasked      = ss.sneakMaskedCount;
        d.coreWarmth       = ss.coreWarmth;
        d.wetness01        = ss.wetness01;
        d.windChillC       = ss.windChillC;
        d.staminaRegenMult = ss.staminaRegenMult;
        d.acousticEnvKind  = ss.acousticEnvKind;
        d.acousticRt60Sec  = ss.acousticRt60Sec;
    }

    float cal6[6] = {201.0f, 7.0f, 17.0f, 12.0f, 0.0f, 20.0f};
    if (RAW_GetCalendarState(cal6)) {
        d.calYear       = cal6[0];
        d.calMonth      = cal6[1];
        d.calDay        = cal6[2];
        d.calHour       = cal6[3];
        d.calDaysPassed = cal6[4];
        d.calTimeScale  = cal6[5];
    }

    RawSavePapyrusHealth sph{};
    if (RAW_GetSavePapyrusHealth(&sph)) {
        d.savesAudited     = sph.savesAudited;
        d.worstSaveHealth  = sph.worstSaveHealth;
        d.worstSaveStrings = sph.worstStringCount;
        d.worstSaveSizeMb  = sph.worstSaveSizeMb;
        d.hitchCount       = sph.hitchCount;
        d.hitchesLast60Sec = sph.hitchesLast60Sec;
        d.worstHitchMs     = sph.worstHitchMs;
        d.p95FrameMs       = sph.p95FrameMs;
    }

    const bool ok = WriteLiveStatusDumpToFile(outPath, d);
    if (ok && outPathWritten) *outPathWritten = outPath;
    return ok;
}

void LiveConfigOnPresent(void (*logFn)(const char* msg)) {
    std::string reloadMsg;
    if (LiveConfigPollReload(&reloadMsg) && logFn && !reloadMsg.empty()) {
        logFn(reloadMsg.c_str());
    }

    uint32_t hotkeyVk = 0;
    {
        std::lock_guard<std::mutex> lk(g_lc.mu);
        if (!g_lc.initialized) return;
        hotkeyVk = g_lc.reloader.Config().statusDumpHotkeyVK;
    }
    if (hotkeyVk == 0u || !IsHostWindowForeground()) {
        std::lock_guard<std::mutex> lk(g_lc.mu);
        g_lc.prevHotkeyDown = false;
        return;
    }

    const bool down = (GetAsyncKeyState(static_cast<int>(hotkeyVk)) & 0x8000) != 0;
    bool edge = false;
    {
        std::lock_guard<std::mutex> lk(g_lc.mu);
        edge = down && !g_lc.prevHotkeyDown;
        g_lc.prevHotkeyDown = down;
    }
    if (edge) {
        std::string written;
        if (LiveConfigWriteStatusDump(&written) && logFn) {
            std::string msg = "[LiveConfig] F10 diagnostics dump written -> " + written;
            logFn(msg.c_str());
        }
    }
}

void LiveConfigShutdown() {
    std::lock_guard<std::mutex> lk(g_lc.mu);
    g_lc.initialized    = false;
    g_lc.prevHotkeyDown = false;
}
#endif

}  // namespace raw::rt
