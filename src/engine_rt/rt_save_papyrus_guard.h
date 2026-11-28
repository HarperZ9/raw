#pragma once
// rt_save_papyrus_guard.h: Wave 48 Clean-Room Skyrim SE `.ESS` Save-File Health Auditor &
// Real-Time Papyrus/Frame Hitch Watchdog (`raw::rt`).
// Zero external dependencies (includes clean-room bounds-checked LZ4 raw block decompressor).
// Audits SSE v12 `.ess` header integrity, file bloat (>32MB Warn / >64MB Critical), and
// Global Data Table 3 Papyrus string-table overflow (>48,000 Warn / >=60,000 Critical / >=65,535 Corrupt).
// Original work, MIT.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace raw::rt {

enum class SaveHealthLevel : uint8_t {
    Healthy  = 0u,
    Warning  = 1u,
    Critical = 2u,
    Corrupt  = 3u,
};

const char* SaveHealthLevelName(SaveHealthLevel lvl) noexcept;

struct EssSaveAuditReport {
    std::string     filePath;
    std::string     fileName;
    uint64_t        fileSizeBytes         = 0ull;
    SaveHealthLevel health                = SaveHealthLevel::Corrupt;
    std::string     reason;

    // Parsed SSE `.ESS` header fields
    uint32_t        version               = 0u;  // 12 = Skyrim SE / AE
    uint32_t        saveNumber            = 0u;
    std::string     playerName;
    uint32_t        playerLevel           = 0u;
    std::string     playerLocation;
    std::string     gameDate;
    std::string     playerRace;
    uint32_t        shotWidth             = 0u;
    uint32_t        shotHeight            = 0u;
    uint16_t        compressionType       = 0u;  // 0=uncompressed, 1=zlib, 2=LZ4

    // Parsed payload & Papyrus string-table audit fields
    uint8_t         formVersion           = 0u;
    uint32_t        uncompressedBodyBytes = 0u;
    uint32_t        compressedBodyBytes   = 0u;
    bool            papyrusAudited        = false;
    uint16_t        papyrusVersion        = 0u;
    uint32_t        papyrusStringCount    = 0u;
    uint32_t        papyrusPayloadBytes   = 0u;
    uint32_t        changeFormCount       = 0u;
};

// Exported C ABI telemetry struct for D3D12Bridge / F10 status dump / tooling.
struct RawSavePapyrusHealth {
    uint8_t  guardEnabled     = 1u;
    uint8_t  worstSaveHealth  = 0u;    // SaveHealthLevel (0..3)
    uint16_t savesAudited     = 0u;
    uint32_t worstStringCount = 0u;
    float    worstSaveSizeMb  = 0.0f;
    uint32_t hitchCount       = 0u;
    uint32_t hitchesLast60Sec = 0u;
    float    worstHitchMs     = 0.0f;
    float    p95FrameMs       = 0.0f;
    float    lastFrameMs      = 0.0f;
    uint64_t totalFrames      = 0ull;
};

// Bounds-checked clean-room LZ4 raw block decompressor (for SSE v12 compressionType == 2).
bool DecompressEssLz4Block(const uint8_t* src, size_t srcSize, uint8_t* dst, size_t expectedOutSize) noexcept;

// Pure `.ESS` binary save-file parser & health auditor.
EssSaveAuditReport AuditEssSaveBytes(const uint8_t* data, size_t size, const std::string& label = "");
EssSaveAuditReport AuditEssSaveFile(const std::string& filePath);

// Read-only bounded directory audit of newest `maxNewestFiles` `.ess` files (default 8).
std::vector<EssSaveAuditReport> AuditSkyrimSavesDirectory(const std::string& savesDir,
                                                          size_t maxNewestFiles = 8u);

// Lightweight deterministic rolling frame-hitch watchdog (zero heap allocations per frame).
class FrameHitchWatchdog {
public:
    static constexpr double kHitchThresholdMs = 50.0;
    static constexpr size_t kFrameWindowSize  = 256u;
    static constexpr size_t kHitchTimeRingCap = 128u;

    void Reset() noexcept;
    void RecordFrame(double frameMs) noexcept;

    [[nodiscard]] uint64_t TotalFrames() const noexcept { return m_totalFrames; }
    [[nodiscard]] uint32_t HitchCount() const noexcept { return m_hitchCount; }
    [[nodiscard]] uint32_t HitchesLast60Sec() const noexcept;
    [[nodiscard]] float    WorstHitchMs() const noexcept { return m_worstHitchMs; }
    [[nodiscard]] float    LastFrameMs() const noexcept { return m_lastFrameMs; }
    [[nodiscard]] float    P95FrameMs() const noexcept;

private:
    std::array<float, kFrameWindowSize>   m_frameRing{};
    size_t                                m_frameCount      = 0u;
    size_t                                m_frameWritePos   = 0u;
    std::array<double, kHitchTimeRingCap> m_hitchStampSec{};
    size_t                                m_hitchRingCount  = 0u;
    size_t                                m_hitchRingWrite  = 0u;
    double                                m_elapsedSec      = 0.0;
    uint64_t                              m_totalFrames     = 0ull;
    uint32_t                              m_hitchCount      = 0u;
    float                                 m_worstHitchMs    = 0.0f;
    float                                 m_lastFrameMs     = 0.0f;
};

// Runtime lifecycle (safe in BOTH raw_engine.dll and RAW_SIM_PURE_TEST=1).
std::string SavePapyrusGuardInit(const std::string& overrideSavesDir = "");
void        SavePapyrusGuardOnPresent(double frameMs);
void        SavePapyrusGuardShutdown();

}  // namespace raw::rt

extern "C" __declspec(dllexport) int RAW_GetSavePapyrusHealth(raw::rt::RawSavePapyrusHealth* out);
