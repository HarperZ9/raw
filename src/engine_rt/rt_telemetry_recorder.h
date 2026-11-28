#pragma once
// rt_telemetry_recorder.h: Wave 52 Zero-Allocation 300-Frame Engine Flight-Recorder Ring Buffer &
// Profiler JSON Dump (`raw::rt`).
// Fixed-size POD circular telemetry ring with zero heap allocations on RecordFrame(), exact rolling
// p50/p95/p99/max/hitch percentile stats, atomic JSON flush on F10 or shutdown, and C ABI export.
// Original work, MIT.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>

namespace raw::rt {

struct FlightFrameSample {
    uint64_t frameIndex        = 0ull;
    double   timestampSec      = 0.0;
    float    frameMs           = 0.0f;
    float    surfaceTempC      = 10.0f;
    float    snowpackM         = 0.0f;
    float    windBeaufort      = 0.0f;
    float    shelterPressure01 = 0.0f;
    uint32_t hitchCount        = 0u;
    uint32_t activeHandles     = 0u;
    uint8_t  audioMood         = 0u;
    uint8_t  modAuditSeverity  = 0u;
    uint16_t reserved          = 0u;
};
static_assert(std::is_trivially_copyable_v<FlightFrameSample>,
              "FlightFrameSample must be trivially copyable POD for zero-allocation ring storage");

struct RawFlightRecorderStats {
    uint8_t  recorderEnabled     = 1u;
    uint8_t  reserved0           = 0u;
    uint16_t ringCapacity        = 300u;
    uint32_t ringFillCount       = 0u;
    uint64_t totalRecordedFrames = 0ull;
    uint32_t hitchFramesOver50ms = 0u;
    uint32_t flushCount          = 0u;
    float    meanFrameMs         = 0.0f;
    float    p50FrameMs          = 0.0f;
    float    p95FrameMs          = 0.0f;
    float    p99FrameMs          = 0.0f;
    float    maxFrameMs          = 0.0f;
};

using FlightRecorderStats = RawFlightRecorderStats;

// Pure zero-allocation 300-frame circular flight recorder ring buffer.
class FlightRecorderRing {
public:
    static constexpr size_t kCapacity        = 300u;
    static constexpr float  kHitchThresholdMs = 50.0f;

    void Reset() noexcept;

    // Zero-allocation circular write of one POD frame sample.
    void RecordFrame(const FlightFrameSample& sample) noexcept;

    // Copies up to `maxCount` samples in strict chronological order (oldest -> newest).
    size_t CopyChronological(FlightFrameSample* outBuf, size_t maxCount) const noexcept;

    // Computes exact rolling percentiles (p50/p95/p99/mean/max) over the active ring + session counters.
    [[nodiscard]] FlightRecorderStats ComputeStats() const noexcept;

    // Serializes summary + chronological frames[] ring into well-formed JSON.
    [[nodiscard]] std::string SerializeJson() const;

    // Atomically flushes JSON report to disk and increments flushCount.
    bool FlushJsonToFile(const char* outPath);

    [[nodiscard]] uint32_t FlushCount() const noexcept { return m_flushCount; }

private:
    std::array<FlightFrameSample, kCapacity> m_ring{};
    size_t                                   m_writePos          = 0u;
    size_t                                   m_fillCount         = 0u;
    uint64_t                                 m_totalRecorded     = 0ull;
    uint32_t                                 m_hitchFramesOver50 = 0u;
    uint32_t                                 m_flushCount        = 0u;
    float                                    m_sessionMaxMs      = 0.0f;
};

// Runtime lifecycle & flush API (safe in BOTH raw_engine.dll and RAW_SIM_PURE_TEST=1).
std::string FlightRecorderInit(const std::string& outDir);
void        FlightRecorderRecordDirect(const FlightFrameSample& sample);
void        FlightRecorderOnPresent(double frameMs);
bool        FlightRecorderFlushJson(const char* outPath = nullptr);
void        FlightRecorderShutdown();

}  // namespace raw::rt

extern "C" __declspec(dllexport) int RAW_GetFlightRecorderStats(raw::rt::RawFlightRecorderStats* out);
