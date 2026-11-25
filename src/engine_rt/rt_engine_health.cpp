// rt_engine_health.cpp: Wave 53 Unified 0-100 Engine Health Scoreboard & Actionable Remediation Adviser (`raw::rt`).
// Original work, MIT.

#include "rt_engine_health.h"

#include "rt_live_config.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <sstream>

#ifndef RAW_SIM_PURE_TEST
#include "rt_crash_forensics.h"
#include "rt_memory_guards.h"
#include "rt_mod_load_audit.h"
#include "rt_save_papyrus_guard.h"
#include "rt_telemetry_recorder.h"
#include "rt_version.h"
#endif

namespace raw::rt {
namespace {

constexpr uint32_t kDefaultHandleCap = 1048576u;

struct AdvisorySlot {
    uint8_t priority = 0u; // Higher = more urgent (3=Critical, 2=Warning, 1=Info)
    char    text[96]{};
};

void PushAdvisory(AdvisorySlot* list, uint32_t& count, uint32_t cap, uint8_t prio, const char* fmt, ...) noexcept {
    if (!list || count >= cap) return;
    AdvisorySlot& slot = list[count++];
    slot.priority = prio;
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(slot.text, sizeof(slot.text), fmt, ap);
    va_end(ap);
    slot.text[sizeof(slot.text) - 1u] = '\0';
}

struct EngineHealthRuntime {
    std::mutex            mu;
    bool                  initialized  = false;
    uint32_t              tickCounter  = 0u;
    uint32_t              refreshCount = 0u;
    RawEngineHealthReport lastReport{};
} g_eh;

}  // namespace

const char* HealthGradeName(HealthGrade grade) noexcept {
    switch (grade) {
        case HealthGrade::Optimal:  return "Optimal";
        case HealthGrade::Healthy:  return "Healthy";
        case HealthGrade::Degraded: return "Degraded";
        case HealthGrade::Critical: return "Critical";
        default:                    return "Critical";
    }
}

RawEngineHealthReport EvaluateEngineHealth(const EngineHealthInputs& in) noexcept {
    RawEngineHealthReport rep{};
    rep.reportEnabled = 1u;

    AdvisorySlot candidates[10]{};
    uint32_t candCount = 0u;

    // 1. Binary Offset & Detour Verification (max 15 pts)
    if (in.zydisVerified) {
        rep.scoreBinaryOffsets = 15u;
    } else {
        rep.scoreBinaryOffsets = 0u;
        PushAdvisory(candidates, candCount, 10u, 3u,
                     "BINARY_OFFSETS_UNVERIFIED: Zydis check failed, struct writes locked");
    }

    // 2. Ref-Handle Cap Safety (max 20 pts, cap = 1,048,576)
    const uint32_t cap = (in.handleCapacity > 0u) ? in.handleCapacity : kDefaultHandleCap;
    const float sat = static_cast<float>(in.activeHandles) / static_cast<float>(cap);
    if (sat >= 0.90f) {
        rep.scoreRefHandles = 0u;
        PushAdvisory(candidates, candCount, 10u, 3u,
                     "REF_HANDLES_CRITICAL: %u/%u (%.0f%%) near 1M CE ref-cap CTD",
                     in.activeHandles, cap, double(sat * 100.0f));
    } else if (sat >= 0.80f) {
        rep.scoreRefHandles = 5u;
        PushAdvisory(candidates, candCount, 10u, 2u,
                     "REF_HANDLES_HIGH: %u/%u (%.0f%%) approaching CE ref-cap",
                     in.activeHandles, cap, double(sat * 100.0f));
    } else if (sat >= 0.65f) {
        rep.scoreRefHandles = 12u;
        PushAdvisory(candidates, candCount, 10u, 2u,
                     "REF_HANDLES_ELEVATED: %u/%u (%.0f%%)",
                     in.activeHandles, cap, double(sat * 100.0f));
    } else {
        rep.scoreRefHandles = 20u;
    }

    // 3. .ESS Save & Papyrus String-Table Health (max 20 pts, ceiling 65,535)
    if (in.corruptSaves > 0u || in.maxSaveStringCount >= 65535u || in.worstSaveHealth >= 3u) {
        rep.scoreSaveHealth = 0u;
        PushAdvisory(candidates, candCount, 10u, 3u,
                     "SAVE_CORRUPT_OVERFLOW: %u/65535 Papyrus strings (%u corrupt)",
                     in.maxSaveStringCount, in.corruptSaves);
    } else if (in.maxSaveStringCount >= 60000u || in.worstSaveHealth == 2u) {
        rep.scoreSaveHealth = 2u;
        PushAdvisory(candidates, candCount, 10u, 3u,
                     "SAVE_STRINGS_CRITICAL: %u/65535 Papyrus strings",
                     in.maxSaveStringCount);
    } else if (in.maxSaveStringCount > 48000u || in.worstSaveHealth == 1u) {
        rep.scoreSaveHealth = (in.maxSaveStringCount >= 55000u) ? 4u : 10u;
        PushAdvisory(candidates, candCount, 10u, 2u,
                     "SAVE_STRINGS_HIGH: %u/65535 Papyrus strings",
                     in.maxSaveStringCount);
    } else {
        rep.scoreSaveHealth = 20u;
    }

    // 4. Mod Load-Order Integrity (max 20 pts)
    if (in.missingMasters > 0u) {
        rep.scoreModOrder = 0u;
        PushAdvisory(candidates, candCount, 10u, 3u,
                     "MOD_MISSING_MASTER: %u missing master(s) - fix plugins.txt before loading",
                     in.missingMasters);
    } else if (in.outOfOrderMasters > 0u || in.corruptModHeaders > 0u || in.fullPluginCount > 254u) {
        rep.scoreModOrder = 2u;
        PushAdvisory(candidates, candCount, 10u, 3u,
                     "MOD_ORDER_CRITICAL: outOfOrder=%u corrupt=%u full=%u/254",
                     in.outOfOrderMasters, in.corruptModHeaders, in.fullPluginCount);
    } else if (in.oversizedEsls > 0u || in.fullPluginCount > 235u) {
        rep.scoreModOrder = 14u;
        PushAdvisory(candidates, candCount, 10u, 2u,
                     "MOD_SLOTS_WARNING: full=%u/254 oversizedESL=%u",
                     in.fullPluginCount, in.oversizedEsls);
    } else {
        rep.scoreModOrder = 20u;
    }

    // 5. Frame Pacing & Hitch Stability (max 17 pts)
    const float p95 = std::isfinite(in.p95FrameMs) ? std::max(0.0f, in.p95FrameMs) : 0.0f;
    if (p95 > 45.0f || in.hitchesOver50ms >= 10u) {
        rep.scoreFramePacing = 0u;
        PushAdvisory(candidates, candCount, 10u, 3u,
                     "FRAME_HITCHES: p95=%.1fms (>45ms) hitches(>50ms)=%u",
                     double(p95), in.hitchesOver50ms);
    } else if (p95 > 25.0f || in.hitchesOver50ms >= 3u) {
        rep.scoreFramePacing = 7u;
        PushAdvisory(candidates, candCount, 10u, 2u,
                     "FRAME_HITCHES: p95 > 25ms (%.1fms, hitches=%u)",
                     double(p95), in.hitchesOver50ms);
    } else if (in.hitchesOver50ms >= 1u) {
        rep.scoreFramePacing = 13u;
        PushAdvisory(candidates, candCount, 10u, 1u,
                     "FRAME_HITCHES_MINOR: %u stutter spike(s) > 50ms",
                     in.hitchesOver50ms);
    } else {
        rep.scoreFramePacing = 17u;
    }

    // 6. Diagnostics & Safety Systems Active (max 8 pts)
    rep.scoreDiagnostics = static_cast<uint8_t>((in.crashForensicsActive ? 4u : 0u) +
                                                (in.flightRecorderActive ? 4u : 0u));
    if (rep.scoreDiagnostics < 8u) {
        PushAdvisory(candidates, candCount, 10u, 1u,
                     "DIAGNOSTICS_PARTIAL: enable CrashForensics & FlightRecorder in raw_engine.ini");
    }

    // Composite score [0..100] & HealthGrade
    const uint32_t total = uint32_t(rep.scoreBinaryOffsets) + uint32_t(rep.scoreRefHandles) +
                           uint32_t(rep.scoreSaveHealth) + uint32_t(rep.scoreModOrder) +
                           uint32_t(rep.scoreFramePacing) + uint32_t(rep.scoreDiagnostics);
    rep.compositeScore = static_cast<uint8_t>(std::min<uint32_t>(total, 100u));

    HealthGrade g = HealthGrade::Critical;
    if (rep.compositeScore >= 85u)      g = HealthGrade::Optimal;
    else if (rep.compositeScore >= 70u) g = HealthGrade::Healthy;
    else if (rep.compositeScore >= 45u) g = HealthGrade::Degraded;
    rep.grade = static_cast<uint8_t>(g);

    // Stable sort advisories by descending priority and emit top 4
    std::stable_sort(candidates, candidates + candCount, [](const AdvisorySlot& a, const AdvisorySlot& b) {
        return a.priority > b.priority;
    });
    const uint32_t outN = std::min<uint32_t>(candCount, 4u);
    rep.advisoryCount = static_cast<uint8_t>(outN);
    for (uint32_t i = 0u; i < outN; ++i) {
        std::memcpy(rep.advisories[i], candidates[i].text, sizeof(rep.advisories[i]));
    }
    return rep;
}

std::string EngineHealthFormatSummary(const RawEngineHealthReport& rep) {
    char buf[320];
    std::snprintf(buf, sizeof(buf),
                  "[EngineHealth] score=%u/100 (%s) | pillars(bin=%u/15 ref=%u/20 save=%u/20 mod=%u/20 pace=%u/17 diag=%u/8) | advisories=%u",
                  static_cast<unsigned>(rep.compositeScore),
                  HealthGradeName(static_cast<HealthGrade>(rep.grade)),
                  static_cast<unsigned>(rep.scoreBinaryOffsets),
                  static_cast<unsigned>(rep.scoreRefHandles),
                  static_cast<unsigned>(rep.scoreSaveHealth),
                  static_cast<unsigned>(rep.scoreModOrder),
                  static_cast<unsigned>(rep.scoreFramePacing),
                  static_cast<unsigned>(rep.scoreDiagnostics),
                  static_cast<unsigned>(rep.advisoryCount));
    std::ostringstream os;
    os << buf;
    for (uint8_t i = 0u; i < rep.advisoryCount && i < 4u; ++i) {
        if (rep.advisories[i][0] != '\0') {
            os << " | ! " << rep.advisories[i];
        }
    }
    return os.str();
}

// --- Runtime Lifecycle & C ABI ---

std::string EngineHealthInit() {
    {
        std::lock_guard<std::mutex> lk(g_eh.mu);
        g_eh.initialized  = true;
        g_eh.tickCounter  = 0u;
        g_eh.refreshCount = 0u;
        g_eh.lastReport   = EvaluateEngineHealth(EngineHealthInputs{});
    }
    EngineHealthRefresh();

    RawEngineHealthReport snap{};
    RAW_GetEngineHealthReport(&snap);
    return EngineHealthFormatSummary(snap);
}

void EngineHealthUpdateDirect(const EngineHealthInputs& in) {
    if (!GetLiveConfig().engineHealthReport) return;
    RawEngineHealthReport rep = EvaluateEngineHealth(in);
    std::lock_guard<std::mutex> lk(g_eh.mu);
    g_eh.initialized = true;
    rep.refreshCount = ++g_eh.refreshCount;
    g_eh.lastReport  = rep;
}

void EngineHealthRefresh() {
    if (!GetLiveConfig().engineHealthReport) return;

    EngineHealthInputs in{};
#ifndef RAW_SIM_PURE_TEST
    in.zydisVerified = AreBinaryOffsetsVerified();

    RawMemoryGuardTelemetry mg{};
    if (RAW_GetMemoryGuardTelemetry(&mg)) {
        in.activeHandles  = mg.refHandlesEstimated;
        in.handleCapacity = kRefHandleCapacity;
    }

    RawSavePapyrusHealth sph{};
    if (RAW_GetSavePapyrusHealth(&sph)) {
        in.maxSaveStringCount = sph.worstStringCount;
        in.worstSaveHealth    = sph.worstSaveHealth;
        in.corruptSaves       = (sph.worstSaveHealth >= 3u) ? 1u : 0u;
        in.p95FrameMs         = sph.p95FrameMs;
        in.hitchesOver50ms    = sph.hitchCount;
    }

    RawModAuditSummary mas{};
    if (RAW_GetModAuditSummary(&mas)) {
        in.missingMasters    = mas.missingMasterCount;
        in.outOfOrderMasters = mas.outOfOrderMasterCount;
        in.corruptModHeaders = mas.corruptHeaderCount;
        in.oversizedEsls     = mas.oversizedEslCount;
        in.fullPluginCount   = mas.fullPluginCount;
    }

    RawFlightRecorderStats frs{};
    if (RAW_GetFlightRecorderStats(&frs)) {
        if (frs.ringFillCount > 0u) {
            in.p95FrameMs      = frs.p95FrameMs;
            in.hitchesOver50ms = std::max(in.hitchesOver50ms, frs.hitchFramesOver50ms);
        }
        in.flightRecorderActive = (frs.recorderEnabled != 0u);
    }

    CrashForensicsTelemetry cft{};
    if (CrashForensicsGetTelemetry(&cft)) {
        in.crashForensicsActive = cft.vehInstalled && cft.enabled;
    }
#endif
    EngineHealthUpdateDirect(in);
}

void EngineHealthOnPresent() {
    if (!GetLiveConfig().engineHealthReport) return;
    bool shouldRefresh = false;
    {
        std::lock_guard<std::mutex> lk(g_eh.mu);
        if (!g_eh.initialized) g_eh.initialized = true;
        // Throttled ~1 Hz (every 60 presents) with zero per-frame allocations!
        if ((g_eh.tickCounter++ % 60u) == 0u) shouldRefresh = true;
    }
    if (shouldRefresh) {
        EngineHealthRefresh();
    }
}

void EngineHealthShutdown() {
    std::lock_guard<std::mutex> lk(g_eh.mu);
    g_eh.initialized = false;
    g_eh.tickCounter = 0u;
}

}  // namespace raw::rt

extern "C" __declspec(dllexport) int RAW_GetEngineHealthReport(raw::rt::RawEngineHealthReport* out) {
    if (!out) return 0;
    const raw::rt::LiveEngineConfig cfg = raw::rt::GetLiveConfig();
    std::lock_guard<std::mutex> lk(raw::rt::g_eh.mu);
    *out = raw::rt::g_eh.lastReport;
    out->reportEnabled = cfg.engineHealthReport ? 1u : 0u;
    return raw::rt::g_eh.initialized ? 1 : 0;
}
