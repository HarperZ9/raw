// rt_boot_selfcheck.cpp: Wave 60 12-Point Live Boot & Runtime Self-Diagnostic Verifier (`raw::rt`).
// Original work, MIT.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "rt_boot_selfcheck.h"

#include "rt_engine_health.h"
#include "rt_frame_governor.h"
#include "rt_live_config.h"
#include "rt_live_dashboard.h"
#include "rt_mod_load_audit.h"
#include "rt_profile_presets.h"
#include "rt_save_papyrus_guard.h"
#include "rt_sdk_bridge.h"
#include "rt_shader_hotreload.h"

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

struct BootSelfCheckRuntime {
    std::mutex            mu;
    bool                  initialized = false;
    std::string           gameDir;
    std::string           outDir;
    uint32_t              runCount    = 0u;
    RawBootSelfCheckState state{};
} g_bsc;

void CopyStrTrunc(char* dst, size_t cap, const std::string& src) noexcept {
    if (!dst || cap == 0u) return;
    const size_t n = std::min(src.size(), cap - 1u);
    if (n > 0u) std::memcpy(dst, src.data(), n);
    dst[n] = '\0';
}

void SetCheckItem(BootCheckItem& item,
                  uint8_t id,
                  const char* name,
                  BootCheckStatus st,
                  const char* detail) noexcept {
    item.id       = id;
    item.status   = static_cast<uint8_t>(st);
    item.reserved = 0u;
    CopyStrTrunc(item.name, sizeof(item.name), name ? name : "");
    CopyStrTrunc(item.detail, sizeof(item.detail), detail ? detail : "");
}

bool AtomicWriteText(const std::string& outPath, const std::string& content) {
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

struct PeAuditInfo {
    bool     valid         = false;
    uint32_t timeDateStamp = 0u;
    uint32_t sizeOfImage   = 0u;
    uint32_t checkSum      = 0u;
    uint16_t machine       = 0u;
    std::string reason;
};

PeAuditInfo AuditPe64File(const std::string& path) {
    PeAuditInfo info{};
    if (path.empty()) {
        info.reason = "empty exe path";
        return info;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        info.reason = "cannot open file";
        return info;
    }
    IMAGE_DOS_HEADER dos{};
    in.read(reinterpret_cast<char*>(&dos), sizeof(dos));
    if (in.gcount() != static_cast<std::streamsize>(sizeof(dos)) || dos.e_magic != IMAGE_DOS_SIGNATURE) {
        info.reason = "invalid DOS MZ signature";
        return info;
    }
    if (dos.e_lfanew <= 0 || dos.e_lfanew > 16 * 1024 * 1024) {
        info.reason = "invalid e_lfanew offset";
        return info;
    }
    in.seekg(static_cast<std::streamoff>(dos.e_lfanew), std::ios::beg);
    IMAGE_NT_HEADERS64 nt{};
    in.read(reinterpret_cast<char*>(&nt), sizeof(nt));
    if (in.gcount() != static_cast<std::streamsize>(sizeof(nt)) ||
        nt.Signature != IMAGE_NT_SIGNATURE ||
        nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
        info.reason = "invalid PE64 NT signature/magic";
        return info;
    }
    info.valid         = true;
    info.machine       = nt.FileHeader.Machine;
    info.timeDateStamp = nt.FileHeader.TimeDateStamp;
    info.sizeOfImage   = nt.OptionalHeader.SizeOfImage;
    info.checkSum      = nt.OptionalHeader.CheckSum;
    info.reason        = "ok";
    return info;
}

std::string ResolveHostExePath(const BootSelfCheckOptions& opts) {
    if (!opts.hostExePath.empty()) return opts.hostExePath;
    if (!opts.gameDir.empty()) {
        std::error_code ec;
        const std::filesystem::path candidate = std::filesystem::path(opts.gameDir) / "SkyrimSE.exe";
        if (std::filesystem::exists(candidate, ec)) return candidate.string();
    }
    char selfPath[MAX_PATH]{};
    const DWORD n = GetModuleFileNameA(nullptr, selfPath, MAX_PATH);
    return (n > 0u && n < MAX_PATH) ? std::string(selfPath, n) : std::string();
}

std::string ResolveDataTablePath(const std::string& explicitPath,
                                 const std::string& gameDir,
                                 const char* fileName) {
    if (!explicitPath.empty()) return explicitPath;
    std::error_code ec;
    if (!gameDir.empty()) {
        const std::filesystem::path p1 = std::filesystem::path(gameDir) / "raw_versions" / fileName;
        if (std::filesystem::exists(p1, ec)) return p1.string();
        const std::filesystem::path p2 = std::filesystem::path(gameDir) / "data" / "versions" / fileName;
        if (std::filesystem::exists(p2, ec)) return p2.string();
    }
    const std::filesystem::path p3 = std::filesystem::path("data") / "versions" / fileName;
    if (std::filesystem::exists(p3, ec)) return p3.string();
    return {};
}

uint32_t CountHexRowsInTsv(const std::string& path, bool* outOpened) {
    if (outOpened) *outOpened = false;
    if (path.empty()) return 0u;
    std::ifstream in(path, std::ios::binary);
    if (!in) return 0u;
    if (outOpened) *outOpened = true;
    uint32_t rows = 0u;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        if (line.find("\t0x") != std::string::npos || line.find("\t0X") != std::string::npos) {
            ++rows;
        }
    }
    return rows;
}

uint32_t PopCount32(uint32_t v) noexcept {
    uint32_t c = 0u;
    while (v != 0u) {
        v &= (v - 1u);
        ++c;
    }
    return c;
}

}  // namespace

const char* BootCheckStatusName(BootCheckStatus st) noexcept {
    switch (st) {
        case BootCheckStatus::Pass: return "PASS";
        case BootCheckStatus::Warn: return "WARN";
        case BootCheckStatus::Fail: return "FAIL";
        default:                    return "UNKNOWN";
    }
}

BootSelfCheckReport RunBootSelfCheck(const BootSelfCheckOptions& opts) {
    BootSelfCheckReport rep{};
    char buf[128];

    // 0. PeBinaryChecksum
    {
        const std::string exePath = ResolveHostExePath(opts);
        const PeAuditInfo pe = AuditPe64File(exePath);
        rep.peTimeDateStamp = pe.timeDateStamp;
        rep.peSizeOfImage   = pe.sizeOfImage;
        rep.peCheckSum      = pe.checkSum;
        if (pe.valid) {
            std::snprintf(buf, sizeof(buf), "PE64 valid (ts=0x%08X img=0x%X csum=0x%08X)",
                          pe.timeDateStamp, pe.sizeOfImage, pe.checkSum);
            SetCheckItem(rep.items[0], 0u, "PeBinaryChecksum", BootCheckStatus::Pass, buf);
        } else {
            std::snprintf(buf, sizeof(buf), "PE64 check failed: %s", pe.reason.c_str());
            SetCheckItem(rep.items[0], 0u, "PeBinaryChecksum", BootCheckStatus::Fail, buf);
        }
    }

    // 1. TsvOffsetTable
    {
        const std::string tsvPath = ResolveDataTablePath(opts.tsvPathOverride, opts.gameDir, "skyrimse-1.7.104.tsv");
        bool opened = false;
        const uint32_t rows = CountHexRowsInTsv(tsvPath, &opened);
        rep.tsvRowCount = rows;
        if (opened && rows >= 50u) {
            std::snprintf(buf, sizeof(buf), "skyrimse-1.7.104.tsv verified (%u hex RVA rows)", rows);
            SetCheckItem(rep.items[1], 1u, "TsvOffsetTable", BootCheckStatus::Pass, buf);
        } else if (opened && rows > 0u) {
            std::snprintf(buf, sizeof(buf), "skyrimse-1.7.104.tsv sparse (%u rows < 50)", rows);
            SetCheckItem(rep.items[1], 1u, "TsvOffsetTable", BootCheckStatus::Warn, buf);
        } else {
            std::snprintf(buf, sizeof(buf), "skyrimse-1.7.104.tsv missing or empty");
            SetCheckItem(rep.items[1], 1u, "TsvOffsetTable", BootCheckStatus::Fail, buf);
        }
    }

    // 2. RttiCatalogIntegrity
    {
        const std::string rttiPath = ResolveDataTablePath(opts.rttiPathOverride, opts.gameDir, "skyrimse-1.7.104-rtti.tsv");
        bool opened = false;
        const uint32_t rows = CountHexRowsInTsv(rttiPath, &opened);
        rep.rttiRowCount = rows;
        if (opened && rows >= 1000u) {
            std::snprintf(buf, sizeof(buf), "skyrimse-1.7.104-rtti.tsv verified (%u RTTI classes)", rows);
            SetCheckItem(rep.items[2], 2u, "RttiCatalogIntegrity", BootCheckStatus::Pass, buf);
        } else if (opened && rows > 0u) {
            std::snprintf(buf, sizeof(buf), "skyrimse-1.7.104-rtti.tsv partial (%u rows < 1000)", rows);
            SetCheckItem(rep.items[2], 2u, "RttiCatalogIntegrity", BootCheckStatus::Warn, buf);
        } else {
            std::snprintf(buf, sizeof(buf), "skyrimse-1.7.104-rtti.tsv missing or empty");
            SetCheckItem(rep.items[2], 2u, "RttiCatalogIntegrity", BootCheckStatus::Fail, buf);
        }
    }

    // 3. ShaderHotReloadHealth
    {
        RawShaderHotReloadState shr{};
        const int active = RAW_GetShaderHotReloadState(&shr);
        if (shr.rejectedEdits > 0u) {
            std::snprintf(buf, sizeof(buf), "rejected=%u syntax error(s) (last=%s)",
                          shr.rejectedEdits,
                          ShaderRejectReasonName(static_cast<ShaderRejectReason>(shr.lastRejectReason)));
            SetCheckItem(rep.items[3], 3u, "ShaderHotReloadHealth", BootCheckStatus::Fail, buf);
        } else if (active) {
            std::snprintf(buf, sizeof(buf), "watcher active (tracked=%u gen=%u rejected=0)",
                          shr.trackedFileCount, shr.reloadGeneration);
            SetCheckItem(rep.items[3], 3u, "ShaderHotReloadHealth", BootCheckStatus::Pass, buf);
        } else {
            std::snprintf(buf, sizeof(buf), "watcher idle (tracked=%u rejected=0)", shr.trackedFileCount);
            SetCheckItem(rep.items[3], 3u, "ShaderHotReloadHealth", BootCheckStatus::Warn, buf);
        }
    }

    // 4. LiveConfigBounds
    {
        const LiveEngineConfig cfg = GetLiveConfig();
        const bool okBounds =
            std::isfinite(cfg.targetFps) && cfg.targetFps >= 30.0f && cfg.targetFps <= 240.0f &&
            std::isfinite(cfg.parryWindowMs) && cfg.parryWindowMs >= 60.0f && cfg.parryWindowMs <= 450.0f &&
            std::isfinite(cfg.dodgeIFrameMs) && cfg.dodgeIFrameMs >= 80.0f && cfg.dodgeIFrameMs <= 500.0f &&
            std::isfinite(cfg.liveAudioGain) && cfg.liveAudioGain >= 0.0f && cfg.liveAudioGain <= 0.50f &&
            cfg.hotReloadIntervalFrames >= 10u && cfg.hotReloadIntervalFrames <= 600u;
        if (okBounds) {
            std::snprintf(buf, sizeof(buf), "in-bounds (targetFps=%.0f parry=%.0fms reload=%uf)",
                          double(cfg.targetFps), double(cfg.parryWindowMs), cfg.hotReloadIntervalFrames);
            SetCheckItem(rep.items[4], 4u, "LiveConfigBounds", BootCheckStatus::Pass, buf);
        } else {
            std::snprintf(buf, sizeof(buf), "live config parameter out of safe bounds");
            SetCheckItem(rep.items[4], 4u, "LiveConfigBounds", BootCheckStatus::Fail, buf);
        }
    }

    // 5. PresetVramHeadroom
    {
        RawProfilePresetState pp{};
        const int active = RAW_GetProfilePresetState(&pp);
        const auto lvl = static_cast<BudgetPressureLevel>(pp.overallPressure);
        if (!active) {
            std::snprintf(buf, sizeof(buf), "preset watchdog not initialized");
            SetCheckItem(rep.items[5], 5u, "PresetVramHeadroom", BootCheckStatus::Warn, buf);
        } else if (lvl == BudgetPressureLevel::Critical) {
            std::snprintf(buf, sizeof(buf), "CRITICAL pressure (RAM=%.0f%% VRAM=%.0f%% -> rec=%s)",
                          double(pp.ramUtilizationPct), double(pp.vramUtilizationPct),
                          QualityPresetName(static_cast<QualityPreset>(pp.recommendedPreset)));
            SetCheckItem(rep.items[5], 5u, "PresetVramHeadroom", BootCheckStatus::Fail, buf);
        } else if (lvl == BudgetPressureLevel::High) {
            std::snprintf(buf, sizeof(buf), "HIGH pressure (RAM=%.0f%% VRAM=%.0f%% -> rec=%s)",
                          double(pp.ramUtilizationPct), double(pp.vramUtilizationPct),
                          QualityPresetName(static_cast<QualityPreset>(pp.recommendedPreset)));
            SetCheckItem(rep.items[5], 5u, "PresetVramHeadroom", BootCheckStatus::Warn, buf);
        } else {
            std::snprintf(buf, sizeof(buf), "%s preset %s pressure (RAM=%.0f%% VRAM=%.0f%%)",
                          QualityPresetName(static_cast<QualityPreset>(pp.activePreset)),
                          BudgetPressureName(lvl),
                          double(pp.ramUtilizationPct), double(pp.vramUtilizationPct));
            SetCheckItem(rep.items[5], 5u, "PresetVramHeadroom", BootCheckStatus::Pass, buf);
        }
    }

    // 6. SaveGameHealth
    {
        RawSavePapyrusHealth sph{};
        (void)RAW_GetSavePapyrusHealth(&sph);
        const auto lvl = static_cast<SaveHealthLevel>(sph.worstSaveHealth);
        if (lvl == SaveHealthLevel::Critical || lvl == SaveHealthLevel::Corrupt || sph.worstStringCount >= 60000u) {
            std::snprintf(buf, sizeof(buf), "worst=%s strings=%u/65535 audited=%u",
                          SaveHealthLevelName(lvl), sph.worstStringCount, static_cast<unsigned>(sph.savesAudited));
            SetCheckItem(rep.items[6], 6u, "SaveGameHealth", BootCheckStatus::Fail, buf);
        } else if (lvl == SaveHealthLevel::Warning) {
            std::snprintf(buf, sizeof(buf), "warning strings=%u/65535 audited=%u",
                          sph.worstStringCount, static_cast<unsigned>(sph.savesAudited));
            SetCheckItem(rep.items[6], 6u, "SaveGameHealth", BootCheckStatus::Warn, buf);
        } else {
            std::snprintf(buf, sizeof(buf), "healthy (audited=%u maxStrings=%u/65535)",
                          static_cast<unsigned>(sph.savesAudited), sph.worstStringCount);
            SetCheckItem(rep.items[6], 6u, "SaveGameHealth", BootCheckStatus::Pass, buf);
        }
    }

    // 7. ModLoadOrderHygiene
    {
        RawModAuditSummary mas{};
        (void)RAW_GetModAuditSummary(&mas);
        const auto sev = static_cast<ModAuditSeverity>(mas.severity);
        const uint32_t issues =
            static_cast<uint32_t>(mas.missingMasterCount) +
            static_cast<uint32_t>(mas.outOfOrderMasterCount) +
            static_cast<uint32_t>(mas.corruptHeaderCount) +
            static_cast<uint32_t>(mas.oversizedEslCount) +
            static_cast<uint32_t>(mas.fullSlotOverflow) +
            static_cast<uint32_t>(mas.lightSlotOverflow);
        if (sev == ModAuditSeverity::Critical || issues > 0u) {
            std::snprintf(buf, sizeof(buf), "severity=%s issues=%u total=%u",
                          ModAuditSeverityName(sev), issues, static_cast<unsigned>(mas.totalPlugins));
            SetCheckItem(rep.items[7], 7u, "ModLoadOrderHygiene", BootCheckStatus::Fail, buf);
        } else if (sev == ModAuditSeverity::Warning) {
            std::snprintf(buf, sizeof(buf), "warning total=%u full=%u/254 esl=%u/4096",
                          static_cast<unsigned>(mas.totalPlugins),
                          static_cast<unsigned>(mas.fullPluginCount),
                          static_cast<unsigned>(mas.lightEslCount));
            SetCheckItem(rep.items[7], 7u, "ModLoadOrderHygiene", BootCheckStatus::Warn, buf);
        } else {
            std::snprintf(buf, sizeof(buf), "clean (total=%u full=%u/254 esl=%u/4096 issues=0)",
                          static_cast<unsigned>(mas.totalPlugins),
                          static_cast<unsigned>(mas.fullPluginCount),
                          static_cast<unsigned>(mas.lightEslCount));
            SetCheckItem(rep.items[7], 7u, "ModLoadOrderHygiene", BootCheckStatus::Pass, buf);
        }
    }

    // 8. SdkAbiReadiness
    {
        const uint32_t ver = RAW_GetSdkVersion();
        RawUnifiedEngineSnapshot snap{};
        snap.structSize = static_cast<uint32_t>(sizeof(RawUnifiedEngineSnapshot));
        const int ok = RAW_GetUnifiedEngineSnapshot(&snap);
        if (ok == 1 && ver == RAW_ENGINE_SDK_VERSION && snap.activeSubsystemMask != 0u) {
            std::snprintf(buf, sizeof(buf), "SDK 0x%08X ready (subsys=%u/17 gmstMods=%u)",
                          ver, PopCount32(snap.activeSubsystemMask), snap.verifiedGmstModules);
            SetCheckItem(rep.items[8], 8u, "SdkAbiReadiness", BootCheckStatus::Pass, buf);
        } else {
            std::snprintf(buf, sizeof(buf), "SDK ABI check failed (ver=0x%08X ok=%d mask=0x%X)",
                          ver, ok, snap.activeSubsystemMask);
            SetCheckItem(rep.items[8], 8u, "SdkAbiReadiness", BootCheckStatus::Fail, buf);
        }
    }

    // 9. DashboardReadiness
    {
        RawLiveDashboardState ld{};
        const int active = RAW_GetLiveDashboardState(&ld);
        if (active && ld.exportCount > 0u && ld.lastExportOk == 0u) {
            std::snprintf(buf, sizeof(buf), "dashboard export failed (count=%u)", ld.exportCount);
            SetCheckItem(rep.items[9], 9u, "DashboardReadiness", BootCheckStatus::Fail, buf);
        } else if (active) {
            std::snprintf(buf, sizeof(buf), "ready (exports=%u cadence=%uf json=%uB html=%uB)",
                          ld.exportCount, ld.autoCadenceFrames, ld.lastJsonBytes, ld.lastHtmlBytes);
            SetCheckItem(rep.items[9], 9u, "DashboardReadiness", BootCheckStatus::Pass, buf);
        } else {
            std::snprintf(buf, sizeof(buf), "dashboard not initialized");
            SetCheckItem(rep.items[9], 9u, "DashboardReadiness", BootCheckStatus::Warn, buf);
        }
    }

    // 10. EngineHealthScore
    {
        RawEngineHealthReport hr{};
        (void)RAW_GetEngineHealthReport(&hr);
        const char* gName = HealthGradeName(static_cast<HealthGrade>(hr.grade));
        if (hr.compositeScore >= 70u) {
            std::snprintf(buf, sizeof(buf), "score=%u/100 (%s, advisories=%u)",
                          static_cast<unsigned>(hr.compositeScore), gName,
                          static_cast<unsigned>(hr.advisoryCount));
            SetCheckItem(rep.items[10], 10u, "EngineHealthScore", BootCheckStatus::Pass, buf);
        } else if (hr.compositeScore >= 45u) {
            std::snprintf(buf, sizeof(buf), "degraded score=%u/100 (%s, advisories=%u)",
                          static_cast<unsigned>(hr.compositeScore), gName,
                          static_cast<unsigned>(hr.advisoryCount));
            SetCheckItem(rep.items[10], 10u, "EngineHealthScore", BootCheckStatus::Warn, buf);
        } else {
            std::snprintf(buf, sizeof(buf), "critical score=%u/100 (%s, advisories=%u)",
                          static_cast<unsigned>(hr.compositeScore), gName,
                          static_cast<unsigned>(hr.advisoryCount));
            SetCheckItem(rep.items[10], 10u, "EngineHealthScore", BootCheckStatus::Fail, buf);
        }
    }

    // 11. FrameGovernorStability
    {
        RawFrameGovernorState fg{};
        (void)RAW_GetFrameGovernorState(&fg);
        const bool okGov =
            std::isfinite(fg.targetFps) && fg.targetFps >= 30.0f && fg.targetFps <= 240.0f &&
            std::isfinite(fg.emaFrameMs) && fg.emaFrameMs > 0.0f &&
            std::isfinite(fg.renderScale) && fg.renderScale >= 0.65f && fg.renderScale <= 1.0f &&
            std::isfinite(fg.volumetricQuality01) && fg.volumetricQuality01 >= 0.50f && fg.volumetricQuality01 <= 1.0f &&
            std::isfinite(fg.shadowDistanceScale) && fg.shadowDistanceScale >= 0.65f && fg.shadowDistanceScale <= 1.0f &&
            std::isfinite(fg.floraLodScale) && fg.floraLodScale >= 0.60f && fg.floraLodScale <= 1.0f &&
            std::isfinite(fg.giRayBudget01) && fg.giRayBudget01 >= 0.50f && fg.giRayBudget01 <= 1.0f;
        if (okGov) {
            std::snprintf(buf, sizeof(buf), "stable %s (target=%.0fFPS render=%.2f vol=%.2f gi=%.2f)",
                          GovernorRegimeName(static_cast<GovernorRegime>(fg.regime)),
                          double(fg.targetFps), double(fg.renderScale),
                          double(fg.volumetricQuality01), double(fg.giRayBudget01));
            SetCheckItem(rep.items[11], 11u, "FrameGovernorStability", BootCheckStatus::Pass, buf);
        } else {
            std::snprintf(buf, sizeof(buf), "frame governor state out of valid finite bounds");
            SetCheckItem(rep.items[11], 11u, "FrameGovernorStability", BootCheckStatus::Fail, buf);
        }
    }

    // Tally summary counts & bitmasks
    for (uint32_t i = 0u; i < kBootSelfCheckCount; ++i) {
        const auto st = static_cast<BootCheckStatus>(rep.items[i].status);
        if (st == BootCheckStatus::Pass) {
            ++rep.passCount;
            rep.passBitmask |= (1u << i);
        } else if (st == BootCheckStatus::Warn) {
            ++rep.warnCount;
        } else {
            ++rep.failCount;
            rep.failBitmask |= (1u << i);
        }
    }
    return rep;
}

std::string FormatBootSelfCheckReport(const BootSelfCheckReport& rep) {
    std::ostringstream os;
    char buf[256];
    const BootCheckStatus overall = (rep.failCount > 0u) ? BootCheckStatus::Fail :
                                    (rep.warnCount > 0u) ? BootCheckStatus::Warn :
                                                           BootCheckStatus::Pass;
    os << "========================================================================\n"
       << "RAW ENGINE RUNTIME - 12-POINT BOOT & RUNTIME SELF-DIAGNOSTIC REPORT\n"
       << "========================================================================\n";
    std::snprintf(buf, sizeof(buf),
                  "Run #%u | Overall: %s | Pass=%u/12 Warn=%u Fail=%u | PassMask=0x%03X FailMask=0x%03X\n\n",
                  rep.runNumber, BootCheckStatusName(overall),
                  rep.passCount, rep.warnCount, rep.failCount,
                  rep.passBitmask, rep.failBitmask);
    os << buf;

    for (uint32_t i = 0u; i < kBootSelfCheckCount; ++i) {
        const auto& it = rep.items[i];
        std::snprintf(buf, sizeof(buf), "  [%02u] [%-4s] %-24s : %s\n",
                      static_cast<unsigned>(it.id + 1u),
                      BootCheckStatusName(static_cast<BootCheckStatus>(it.status)),
                      it.name, it.detail);
        os << buf;
    }
    return os.str();
}

bool BootSelfCheckRunAndWrite(const char* overrideOutDir, std::string* outSummary) {
    BootSelfCheckOptions opts{};
    uint32_t nextRun = 1u;
    {
        std::lock_guard<std::mutex> lk(g_bsc.mu);
        opts.gameDir = g_bsc.gameDir;
        opts.outDir  = (overrideOutDir && overrideOutDir[0] != '\0') ? std::string(overrideOutDir) : g_bsc.outDir;
        nextRun      = g_bsc.runCount + 1u;
    }

    BootSelfCheckReport rep = RunBootSelfCheck(opts);
    rep.runNumber = nextRun;

    const std::string text = FormatBootSelfCheckReport(rep);
    std::string reportPath;
    bool wrote = false;
    if (!opts.outDir.empty()) {
        reportPath = (std::filesystem::path(opts.outDir) / "boot_selfcheck.txt").string();
        wrote      = AtomicWriteText(reportPath, text);
    }

    const BootCheckStatus overall = (rep.failCount > 0u) ? BootCheckStatus::Fail :
                                    (rep.warnCount > 0u) ? BootCheckStatus::Warn :
                                                           BootCheckStatus::Pass;

    {
        std::lock_guard<std::mutex> lk(g_bsc.mu);
        g_bsc.runCount                 = nextRun;
        g_bsc.state.initialized        = g_bsc.initialized ? 1u : 0u;
        g_bsc.state.overallStatus      = static_cast<uint8_t>(overall);
        g_bsc.state.lastReportWritten  = wrote ? 1u : 0u;
        g_bsc.state.runCount           = nextRun;
        g_bsc.state.totalChecks        = kBootSelfCheckCount;
        g_bsc.state.passCount          = rep.passCount;
        g_bsc.state.warnCount          = rep.warnCount;
        g_bsc.state.failCount          = rep.failCount;
        g_bsc.state.passBitmask        = rep.passBitmask;
        g_bsc.state.failBitmask        = rep.failBitmask;
        g_bsc.state.tsvRowCount        = rep.tsvRowCount;
        g_bsc.state.rttiRowCount       = rep.rttiRowCount;
        CopyStrTrunc(g_bsc.state.lastReportPath, sizeof(g_bsc.state.lastReportPath), reportPath);
    }

    if (outSummary) {
        char buf[288];
        std::snprintf(buf, sizeof(buf),
                      "[BootSelfCheck] run=%u overall=%s pass=%u/12 warn=%u fail=%u (passMask=0x%03X failMask=0x%03X) tsv=%u rtti=%u wrote=%s",
                      rep.runNumber, BootCheckStatusName(overall),
                      rep.passCount, rep.warnCount, rep.failCount,
                      rep.passBitmask, rep.failBitmask,
                      rep.tsvRowCount, rep.rttiRowCount,
                      wrote ? "yes" : "no");
        *outSummary = buf;
    }
    return rep.failCount == 0u;
}

std::string BootSelfCheckInit(const std::string& gameDir, const std::string& outDir) {
    {
        std::lock_guard<std::mutex> lk(g_bsc.mu);
        g_bsc.initialized = true;
        g_bsc.gameDir     = gameDir;
        g_bsc.outDir      = outDir;
        g_bsc.runCount    = 0u;
        g_bsc.state       = {};
        g_bsc.state.initialized = 1u;
        g_bsc.state.totalChecks = kBootSelfCheckCount;
    }
    std::string summary;
    (void)BootSelfCheckRunAndWrite(nullptr, &summary);
    return summary;
}

void BootSelfCheckShutdown() {
    std::lock_guard<std::mutex> lk(g_bsc.mu);
    g_bsc.initialized       = false;
    g_bsc.state.initialized = 0u;
    g_bsc.gameDir.clear();
    g_bsc.outDir.clear();
}

}  // namespace raw::rt

extern "C" __declspec(dllexport) int RAW_GetBootSelfCheckState(raw::rt::RawBootSelfCheckState* out) {
    if (!out) return 0;
    std::lock_guard<std::mutex> lk(raw::rt::g_bsc.mu);
    *out = raw::rt::g_bsc.state;
    return raw::rt::g_bsc.initialized ? 1 : 0;
}
