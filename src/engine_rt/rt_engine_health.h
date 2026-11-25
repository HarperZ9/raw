#pragma once
// rt_engine_health.h: Wave 53 Unified 0-100 Engine Health Scoreboard & Actionable Remediation Adviser (`raw::rt`).
// Unifies all raw_engine.dll runtime stability & performance pillars (Zydis binary offsets,
// 1M ref-handle cap, .ESS save Papyrus strings, TES4 mod load-order, frame pacing/hitches,
// and safety diagnostics) into a deterministic 0-100 composite score + prioritized remediation advisories.
// Zero per-frame heap allocations; zero external dependencies.
// Original work, MIT.

#include <cstdint>
#include <string>

namespace raw::rt {

enum class HealthGrade : uint8_t {
    Optimal  = 0u, // >= 85
    Healthy  = 1u, // 70 .. 84
    Degraded = 2u, // 45 .. 69
    Critical = 3u, // < 45
};

const char* HealthGradeName(HealthGrade grade) noexcept;

struct EngineHealthInputs {
    // 1. Binary Offset & Detour Verification (max 15 pts)
    bool     zydisVerified         = true;

    // 2. Ref-Handle Cap Safety (max 20 pts, capacity 1,048,576)
    uint32_t activeHandles         = 0u;
    uint32_t handleCapacity        = 1048576u;

    // 3. .ESS Save & Papyrus String-Table Health (max 20 pts, ceiling 65,535)
    uint32_t maxSaveStringCount    = 0u;
    uint32_t corruptSaves          = 0u;
    uint8_t  worstSaveHealth       = 0u; // SaveHealthLevel 0..3

    // 4. Mod Load-Order Integrity (max 20 pts)
    uint32_t missingMasters        = 0u;
    uint32_t outOfOrderMasters     = 0u;
    uint32_t corruptModHeaders     = 0u;
    uint32_t oversizedEsls         = 0u;
    uint32_t fullPluginCount       = 0u; // max 254

    // 5. Frame Pacing & Hitch Stability (max 17 pts)
    float    p95FrameMs            = 16.6f;
    uint32_t hitchesOver50ms       = 0u;

    // 6. Diagnostics & Safety Systems Active (max 8 pts)
    bool     crashForensicsActive  = true;
    bool     flightRecorderActive  = true;
};

// Exported C ABI health report struct (zero heap allocations).
struct RawEngineHealthReport {
    uint8_t  reportEnabled         = 1u;
    uint8_t  compositeScore        = 100u; // Clamped [0..100]
    uint8_t  grade                 = 0u;   // HealthGrade (0=Optimal..3=Critical)
    uint8_t  advisoryCount         = 0u;   // 0..4 prioritized remediation advisories

    // Individual pillar scores for transparent telemetry
    uint8_t  scoreBinaryOffsets    = 15u;  // /15
    uint8_t  scoreRefHandles       = 20u;  // /20
    uint8_t  scoreSaveHealth       = 20u;  // /20
    uint8_t  scoreModOrder         = 20u;  // /20
    uint8_t  scoreFramePacing      = 17u;  // /17
    uint8_t  scoreDiagnostics      = 8u;   // /8
    uint16_t reserved              = 0u;

    uint32_t refreshCount          = 0u;
    char     advisories[4][96]{};
};

// Pure deterministic 6-pillar health evaluator (zero allocations).
RawEngineHealthReport EvaluateEngineHealth(const EngineHealthInputs& in) noexcept;

// Formats a concise single/multi-line summary for startup logs & F10 dump.
std::string EngineHealthFormatSummary(const RawEngineHealthReport& rep);

// Runtime lifecycle hooks (safe in BOTH raw_engine.dll and RAW_SIM_PURE_TEST=1).
std::string EngineHealthInit();
void        EngineHealthUpdateDirect(const EngineHealthInputs& in);
void        EngineHealthRefresh();
void        EngineHealthOnPresent();
void        EngineHealthShutdown();

}  // namespace raw::rt

extern "C" __declspec(dllexport) int RAW_GetEngineHealthReport(raw::rt::RawEngineHealthReport* out);
