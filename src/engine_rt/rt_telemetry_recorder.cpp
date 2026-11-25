// rt_telemetry_recorder.cpp: Wave 52 Zero-Allocation 300-Frame Engine Flight-Recorder Ring Buffer &
// Profiler JSON Dump (`raw::rt`).
// Original work, MIT.

#include "rt_telemetry_recorder.h"

#include "rt_live_config.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#ifndef RAW_SIM_PURE_TEST
#include "rt_adaptive_audio_director.h"
#include "rt_memory_guards.h"
#include "rt_mod_load_audit.h"
#include "rt_npc_behavior.h"
#include "rt_save_papyrus_guard.h"
#include "rt_world_sim.h"
#endif

namespace raw::rt {
namespace {

inline float SafeF(float v) noexcept { return std::isfinite(v) ? v : 0.0f; }
inline double SafeD(double v) noexcept { return std::isfinite(v) ? v : 0.0; }

bool AtomicWriteJsonFile(const char* path, const std::string& payload) {
    if (!path || !*path) return false;
    try {
        const std::filesystem::path target(path);
        if (target.has_parent_path()) {
            std::error_code ec;
            std::filesystem::create_directories(target.parent_path(), ec);
        }
        const std::string tmpPath = std::string(path) + ".tmp";
        {
            std::ofstream ofs(tmpPath, std::ios::binary | std::ios::trunc);
            if (!ofs.is_open()) return false;
            ofs.write(payload.data(), static_cast<std::streamsize>(payload.size()));
            ofs.flush();
            if (!ofs.good()) return false;
        }
#ifdef _WIN32
        if (MoveFileExA(tmpPath.c_str(), path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            return true;
        }
#endif
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

struct FlightRecorderRuntime {
    std::mutex         mu;
    bool               initialized = false;
    std::string        defaultJsonPath;
    double             elapsedSec  = 0.0;
    uint64_t           nextFrameIdx = 0ull;
    FlightRecorderRing ring{};
} g_fr;

}  // namespace

void FlightRecorderRing::Reset() noexcept {
    m_ring.fill(FlightFrameSample{});
    m_writePos          = 0u;
    m_fillCount         = 0u;
    m_totalRecorded     = 0ull;
    m_hitchFramesOver50 = 0u;
    m_flushCount        = 0u;
    m_sessionMaxMs      = 0.0f;
}

void FlightRecorderRing::RecordFrame(const FlightFrameSample& sample) noexcept {
    m_ring[m_writePos] = sample;
    m_writePos = (m_writePos + 1u) % kCapacity;
    if (m_fillCount < kCapacity) ++m_fillCount;
    ++m_totalRecorded;

    const float fms = SafeF(sample.frameMs);
    if (fms > m_sessionMaxMs) m_sessionMaxMs = fms;
    if (fms > kHitchThresholdMs) ++m_hitchFramesOver50;
}

size_t FlightRecorderRing::CopyChronological(FlightFrameSample* outBuf, size_t maxCount) const noexcept {
    if (!outBuf || maxCount == 0u || m_fillCount == 0u) return 0u;
    const size_t n = std::min(m_fillCount, maxCount);
    const size_t start = (m_fillCount < kCapacity) ? 0u : m_writePos;
    for (size_t i = 0u; i < n; ++i) {
        outBuf[i] = m_ring[(start + i) % kCapacity];
    }
    return n;
}

FlightRecorderStats FlightRecorderRing::ComputeStats() const noexcept {
    FlightRecorderStats st{};
    st.recorderEnabled     = 1u;
    st.ringCapacity        = static_cast<uint16_t>(kCapacity);
    st.ringFillCount       = static_cast<uint32_t>(m_fillCount);
    st.totalRecordedFrames = m_totalRecorded;
    st.hitchFramesOver50ms = m_hitchFramesOver50;
    st.flushCount          = m_flushCount;
    st.maxFrameMs          = m_sessionMaxMs;

    if (m_fillCount == 0u) return st;

    std::array<float, kCapacity> sorted{};
    double sum = 0.0;
    const size_t start = (m_fillCount < kCapacity) ? 0u : m_writePos;
    for (size_t i = 0u; i < m_fillCount; ++i) {
        const float ms = SafeF(m_ring[(start + i) % kCapacity].frameMs);
        sorted[i] = ms;
        sum += double(ms);
    }
    std::sort(sorted.begin(), sorted.begin() + static_cast<ptrdiff_t>(m_fillCount));

    auto pctAt = [&](double p) -> float {
        const size_t idx = std::min(m_fillCount - 1u,
                                    static_cast<size_t>(std::floor(double(m_fillCount - 1u) * p + 0.5)));
        return sorted[idx];
    };

    st.meanFrameMs = static_cast<float>(sum / double(m_fillCount));
    st.p50FrameMs  = pctAt(0.50);
    st.p95FrameMs  = pctAt(0.95);
    st.p99FrameMs  = pctAt(0.99);
    return st;
}

std::string FlightRecorderRing::SerializeJson() const {
    const FlightRecorderStats st = ComputeStats();
    std::array<FlightFrameSample, kCapacity> chrono{};
    const size_t n = CopyChronological(chrono.data(), kCapacity);

    std::ostringstream os;
    os << std::fixed << std::setprecision(3);
    os << "{\n"
       << "  \"schema\": \"raw_flight_recorder_v1\",\n"
       << "  \"summary\": {\n"
       << "    \"totalRecordedFrames\": " << st.totalRecordedFrames << ",\n"
       << "    \"ringFillCount\": " << st.ringFillCount << ",\n"
       << "    \"ringCapacity\": " << st.ringCapacity << ",\n"
       << "    \"meanFrameMs\": " << SafeF(st.meanFrameMs) << ",\n"
       << "    \"p50FrameMs\": " << SafeF(st.p50FrameMs) << ",\n"
       << "    \"p95FrameMs\": " << SafeF(st.p95FrameMs) << ",\n"
       << "    \"p99FrameMs\": " << SafeF(st.p99FrameMs) << ",\n"
       << "    \"maxFrameMs\": " << SafeF(st.maxFrameMs) << ",\n"
       << "    \"hitchFramesOver50ms\": " << st.hitchFramesOver50ms << ",\n"
       << "    \"flushCount\": " << st.flushCount << "\n"
       << "  },\n"
       << "  \"frames\": [\n";

    for (size_t i = 0u; i < n; ++i) {
        const FlightFrameSample& f = chrono[i];
        os << "    {\"frameIndex\":" << f.frameIndex
           << ",\"timestampSec\":" << SafeD(f.timestampSec)
           << ",\"frameMs\":" << SafeF(f.frameMs)
           << ",\"surfaceTempC\":" << SafeF(f.surfaceTempC)
           << ",\"snowpackM\":" << SafeF(f.snowpackM)
           << ",\"windBeaufort\":" << SafeF(f.windBeaufort)
           << ",\"audioMood\":" << static_cast<unsigned>(f.audioMood)
           << ",\"shelterPressure01\":" << SafeF(f.shelterPressure01)
           << ",\"hitchCount\":" << f.hitchCount
           << ",\"activeHandles\":" << f.activeHandles
           << ",\"modAuditSeverity\":" << static_cast<unsigned>(f.modAuditSeverity)
           << "}" << (i + 1u < n ? ",\n" : "\n");
    }
    os << "  ]\n}\n";
    return os.str();
}

bool FlightRecorderRing::FlushJsonToFile(const char* outPath) {
    if (!outPath || !*outPath) return false;
    const std::string json = SerializeJson();
    if (!AtomicWriteJsonFile(outPath, json)) return false;
    ++m_flushCount;
    return true;
}

// --- Runtime Lifecycle & C ABI ---

std::string FlightRecorderInit(const std::string& outDir) {
    const LiveEngineConfig cfg = GetLiveConfig();
    std::string target;
    if (!outDir.empty()) {
        target = outDir;
        if (target.back() != '\\' && target.back() != '/') target.push_back('\\');
        target += "session_flight_recorder.json";
    }
    {
        std::lock_guard<std::mutex> lk(g_fr.mu);
        g_fr.initialized     = true;
        g_fr.defaultJsonPath = target;
        g_fr.elapsedSec      = 0.0;
        g_fr.nextFrameIdx    = 0ull;
        g_fr.ring.Reset();
    }
    char buf[256];
    std::snprintf(buf, sizeof(buf),
                  "[FlightRecorder] initialized (FlightRecorder=%u, ring=300 frames zero-alloc, dump=%s)",
                  cfg.flightRecorderEnabled ? 1u : 0u, target.empty() ? "<memory-only>" : target.c_str());
    return buf;
}

void FlightRecorderRecordDirect(const FlightFrameSample& sample) {
    if (!GetLiveConfig().flightRecorderEnabled) return;
    std::lock_guard<std::mutex> lk(g_fr.mu);
    if (!g_fr.initialized) g_fr.initialized = true;
    g_fr.ring.RecordFrame(sample);
}

void FlightRecorderOnPresent(double frameMs) {
    if (!GetLiveConfig().flightRecorderEnabled) return;

    FlightFrameSample s{};
    s.frameMs = static_cast<float>(std::clamp(std::isfinite(frameMs) ? frameMs : 0.0, 0.0, 60000.0));

#ifndef RAW_SIM_PURE_TEST
    RawWorldSimTelemetry ws{};
    if (RAW_GetWorldSimTelemetry(&ws)) {
        s.surfaceTempC = ws.ambientTempC;
        s.snowpackM    = ws.snowDepthU / 70.0f; // Skyrim units (~70u/m) -> meters
        s.windBeaufort = ws.beaufortScale;
    }
    RawAudioDirectorState ads{};
    if (RAW_GetAudioDirectorState(&ads)) {
        s.audioMood = ads.activeMood;
    }
    RawNpcBehaviorState nbs{};
    if (RAW_GetNpcBehaviorState(&nbs)) {
        s.shelterPressure01 = nbs.shelterPressure01;
    }
    RawSavePapyrusHealth sph{};
    if (RAW_GetSavePapyrusHealth(&sph)) {
        s.hitchCount = sph.hitchCount;
    }
    RawMemoryGuardTelemetry mg{};
    if (RAW_GetMemoryGuardTelemetry(&mg)) {
        s.activeHandles = mg.refHandlesEstimated;
    }
    RawModAuditSummary mas{};
    if (RAW_GetModAuditSummary(&mas)) {
        s.modAuditSeverity = mas.severity;
    }
#endif

    std::lock_guard<std::mutex> lk(g_fr.mu);
    if (!g_fr.initialized) g_fr.initialized = true;
    g_fr.elapsedSec += std::min(std::max(0.0, frameMs), 10000.0) * 0.001;
    s.frameIndex   = g_fr.nextFrameIdx++;
    s.timestampSec = g_fr.elapsedSec;
    g_fr.ring.RecordFrame(s);
}

bool FlightRecorderFlushJson(const char* outPath) {
    std::lock_guard<std::mutex> lk(g_fr.mu);
    const char* target = (outPath && *outPath) ? outPath : g_fr.defaultJsonPath.c_str();
    if (!target || !*target) return false;
    return g_fr.ring.FlushJsonToFile(target);
}

void FlightRecorderShutdown() {
    std::lock_guard<std::mutex> lk(g_fr.mu);
    if (g_fr.initialized && !g_fr.defaultJsonPath.empty() &&
        g_fr.ring.ComputeStats().totalRecordedFrames > 0ull) {
        g_fr.ring.FlushJsonToFile(g_fr.defaultJsonPath.c_str());
    }
    g_fr.initialized = false;
    g_fr.ring.Reset();
}

}  // namespace raw::rt

extern "C" __declspec(dllexport) int RAW_GetFlightRecorderStats(raw::rt::RawFlightRecorderStats* out) {
    if (!out) return 0;
    const raw::rt::LiveEngineConfig cfg = raw::rt::GetLiveConfig();
    std::lock_guard<std::mutex> lk(raw::rt::g_fr.mu);
    *out = raw::rt::g_fr.ring.ComputeStats();
    out->recorderEnabled = cfg.flightRecorderEnabled ? 1u : 0u;
    return raw::rt::g_fr.initialized ? 1 : 0;
}
