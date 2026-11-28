// rt_save_papyrus_guard.cpp: Wave 48 Clean-Room Skyrim SE `.ESS` Save-File Health Auditor &
// Real-Time Papyrus/Frame Hitch Watchdog (`raw::rt`).
// Original work, MIT.

#include "rt_save_papyrus_guard.h"

#include "rt_live_config.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace raw::rt {
namespace {

constexpr char     kEssMagic[13]         = {'T','E','S','V','_','S','A','V','E','G','A','M','E'};
constexpr uint32_t kStringWarnThreshold  = 48000u;
constexpr uint32_t kStringCritThreshold  = 60000u;
constexpr uint32_t kStringCorruptCeiling = 65535u;
constexpr uint64_t kSizeWarnBytes        = 32ull * 1024ull * 1024ull;
constexpr uint64_t kSizeCritBytes        = 64ull * 1024ull * 1024ull;
constexpr uint32_t kMaxLz4DecodeBytes    = 192u * 1024u * 1024u; // 192 MB safety cap

struct ByteCursor {
    const uint8_t* ptr  = nullptr;
    size_t         size = 0u;
    size_t         pos  = 0u;

    bool Has(size_t n) const noexcept { return n <= size && pos <= size - n; }

    bool ReadU8(uint8_t& out) noexcept {
        if (!Has(1u)) return false;
        out = ptr[pos++];
        return true;
    }
    bool ReadU16(uint16_t& out) noexcept {
        if (!Has(2u)) return false;
        std::memcpy(&out, ptr + pos, 2u);
        pos += 2u;
        return true;
    }
    bool ReadU32(uint32_t& out) noexcept {
        if (!Has(4u)) return false;
        std::memcpy(&out, ptr + pos, 4u);
        pos += 4u;
        return true;
    }
    bool ReadU64(uint64_t& out) noexcept {
        if (!Has(8u)) return false;
        std::memcpy(&out, ptr + pos, 8u);
        pos += 8u;
        return true;
    }
    bool ReadF32(float& out) noexcept {
        if (!Has(4u)) return false;
        std::memcpy(&out, ptr + pos, 4u);
        pos += 4u;
        return true;
    }
    bool ReadWString(std::string& out, size_t maxLen = 4096u) {
        uint16_t len = 0u;
        if (!ReadU16(len) || len > maxLen || !Has(len)) return false;
        out.assign(reinterpret_cast<const char*>(ptr + pos), len);
        pos += len;
        return true;
    }
};

void EscalateHealth(SaveHealthLevel& cur, SaveHealthLevel next, std::string& reason, const std::string& msg) {
    if (static_cast<uint8_t>(next) > static_cast<uint8_t>(cur)) {
        cur = next;
    }
    if (!msg.empty()) {
        if (!reason.empty()) reason += "; ";
        reason += msg;
    }
}

// Parses uncompressed SSE body (starting at formVersion) to extract Global Data Table 3 ->
// Papyrus (type == 1001) string-table count.
void AuditUncompressedEssBody(const uint8_t* body, size_t bodySize, size_t bodyFileOffset, EssSaveAuditReport& rep) {
    ByteCursor cur{body, bodySize, 0u};
    if (!cur.ReadU8(rep.formVersion)) return;

    uint32_t pluginInfoSize = 0u;
    if (!cur.ReadU32(pluginInfoSize) || !cur.Has(pluginInfoSize)) return;
    cur.pos += pluginInfoSize;

    // FileLocationTable (10 leading uint32 fields):
    // [0]=formIDArrayCountOff, [1]=unknownTable3Off, [2]=gdt1Off, [3]=gdt2Off, [4]=changeFormsOff,
    // [5]=gdt3Off, [6]=gdt1Count, [7]=gdt2Count, [8]=gdt3Count, [9]=changeFormCount
    uint32_t flt[10]{};
    for (int i = 0; i < 10; ++i) {
        if (!cur.ReadU32(flt[i])) return;
    }
    const uint32_t gdt3Off   = flt[5];
    const uint32_t gdt3Count = flt[8];
    rep.changeFormCount      = flt[9];

    // Convert file-relative gdt3Off into body-buffer-relative offset cleanly
    size_t gdt3Pos = 0u;
    if (gdt3Off >= bodyFileOffset && size_t(gdt3Off - bodyFileOffset) < bodySize) {
        gdt3Pos = size_t(gdt3Off - bodyFileOffset);
    } else if (gdt3Off < bodySize) {
        gdt3Pos = gdt3Off;
    } else {
        return;
    }

    // Walk Global Data Table 3 entries (SSE stores gdt3Count off-by-one excluding type 1005,
    // so walk up to gdt3Count + 2 bounded strictly by valid GlobalData3 types 1000..1006).
    const uint32_t maxEntries = std::min<uint32_t>(gdt3Count + 2u, 16u);
    size_t pos = gdt3Pos;
    for (uint32_t i = 0u; i < maxEntries; ++i) {
        if (pos + 8u > bodySize) break;
        uint32_t gdType = 0u, gdLen = 0u;
        std::memcpy(&gdType, body + pos, 4u);
        std::memcpy(&gdLen,  body + pos + 4u, 4u);
        if (gdType < 1000u || gdType > 1006u) break;
        if (size_t(gdLen) > bodySize - (pos + 8u)) break;

        if (gdType == 1001u && gdLen >= 6u) {
            uint16_t papyrusVer = 0u;
            uint32_t stringCount = 0u;
            std::memcpy(&papyrusVer,  body + pos + 8u,  2u);
            std::memcpy(&stringCount, body + pos + 10u, 4u);
            rep.papyrusAudited      = true;
            rep.papyrusVersion      = papyrusVer;
            rep.papyrusStringCount  = stringCount;
            rep.papyrusPayloadBytes = gdLen;
            break;
        }
        pos += 8u + size_t(gdLen);
    }
}

std::string DefaultSkyrimSavesDir() {
#ifdef _WIN32
    char prof[MAX_PATH]{};
    const DWORD n = GetEnvironmentVariableA("USERPROFILE", prof, MAX_PATH);
    if (n > 0u && n < MAX_PATH) {
        return std::string(prof) + "\\Documents\\My Games\\Skyrim Special Edition\\Saves";
    }
#endif
    return {};
}

struct SavePapyrusGuardState {
    std::mutex         mu;
    bool               initialized      = false;
    uint16_t           savesAudited     = 0u;
    uint8_t            worstSaveHealth  = 0u;
    uint32_t           worstStringCount = 0u;
    float              worstSaveSizeMb  = 0.0f;
    FrameHitchWatchdog watchdog{};
} g_guard;

}  // namespace

const char* SaveHealthLevelName(SaveHealthLevel lvl) noexcept {
    switch (lvl) {
        case SaveHealthLevel::Healthy:  return "Healthy";
        case SaveHealthLevel::Warning:  return "Warning";
        case SaveHealthLevel::Critical: return "Critical";
        case SaveHealthLevel::Corrupt:  return "Corrupt";
        default:                        return "Unknown";
    }
}

bool DecompressEssLz4Block(const uint8_t* src, size_t srcSize, uint8_t* dst, size_t expectedOutSize) noexcept {
    if (!src || !dst || srcSize == 0u || expectedOutSize == 0u || expectedOutSize > kMaxLz4DecodeBytes) {
        return false;
    }
    size_t ip = 0u;
    size_t op = 0u;
    while (ip < srcSize) {
        const uint8_t token = src[ip++];
        size_t litLen = static_cast<size_t>(token >> 4u);
        if (litLen == 15u) {
            while (ip < srcSize) {
                const uint8_t extra = src[ip++];
                litLen += extra;
                if (extra != 255u) break;
                if (litLen > expectedOutSize) return false;
            }
        }
        if (litLen > srcSize - ip || litLen > expectedOutSize - op) {
            return false;
        }
        if (litLen > 0u) {
            std::memcpy(dst + op, src + ip, litLen);
            ip += litLen;
            op += litLen;
        }
        if (ip >= srcSize) break;

        if (srcSize - ip < 2u) return false;
        const uint16_t offset = static_cast<uint16_t>(src[ip] | (uint16_t(src[ip + 1u]) << 8u));
        ip += 2u;
        if (offset == 0u || offset > op) return false;

        size_t matchLen = static_cast<size_t>(token & 0x0Fu) + 4u;
        if (matchLen == 19u) {
            while (ip < srcSize) {
                const uint8_t extra = src[ip++];
                matchLen += extra;
                if (extra != 255u) break;
                if (matchLen > expectedOutSize) return false;
            }
        }
        if (matchLen > expectedOutSize - op) return false;

        // Byte-by-byte copy handles overlapping RLE matches accurately!
        size_t matchPos = op - offset;
        for (size_t k = 0u; k < matchLen; ++k) {
            dst[op++] = dst[matchPos++];
        }
    }
    return op == expectedOutSize;
}

EssSaveAuditReport AuditEssSaveBytes(const uint8_t* data, size_t size, const std::string& label) {
    EssSaveAuditReport rep{};
    rep.fileName      = label;
    rep.fileSizeBytes = size;
    rep.health        = SaveHealthLevel::Corrupt;

    if (!data || size < 32u) {
        rep.reason = "truncated file (<32 bytes)";
        return rep;
    }
    if (std::memcmp(data, kEssMagic, sizeof(kEssMagic)) != 0) {
        rep.reason = "invalid magic (expected TESV_SAVEGAME)";
        return rep;
    }

    ByteCursor cur{data, size, 13u};
    uint32_t headerSize = 0u;
    if (!cur.ReadU32(headerSize)) {
        rep.reason = "truncated headerSize";
        return rep;
    }
    if (headerSize < 42u || headerSize > 65536u || !cur.Has(headerSize)) {
        rep.reason = "corrupted or truncated header block";
        return rep;
    }

    const size_t headerEnd = cur.pos + headerSize;
    ByteCursor hcur{data, headerEnd, cur.pos};

    uint16_t playerSex = 0u;
    float curExp = 0.0f, lvlExp = 0.0f;
    uint64_t filetime = 0ull;

    if (!hcur.ReadU32(rep.version) ||
        !hcur.ReadU32(rep.saveNumber) ||
        !hcur.ReadWString(rep.playerName) ||
        !hcur.ReadU32(rep.playerLevel) ||
        !hcur.ReadWString(rep.playerLocation) ||
        !hcur.ReadWString(rep.gameDate) ||
        !hcur.ReadWString(rep.playerRace) ||
        !hcur.ReadU16(playerSex) ||
        !hcur.ReadF32(curExp) ||
        !hcur.ReadF32(lvlExp) ||
        !hcur.ReadU64(filetime) ||
        !hcur.ReadU32(rep.shotWidth) ||
        !hcur.ReadU32(rep.shotHeight)) {
        rep.reason = "truncated SSE header fields";
        return rep;
    }

    if (rep.version != 12u && rep.version != 9u) {
        rep.reason = "unsupported .ESS version (" + std::to_string(rep.version) + ")";
        return rep;
    }

    if (rep.version >= 12u) {
        if (!hcur.ReadU16(rep.compressionType) || rep.compressionType > 2u) {
            rep.reason = "invalid SSE compressionType";
            return rep;
        }
    }

    // Validate screenshot dimensions & bounds without 32-bit overflow
    if (rep.shotWidth > 4096u || rep.shotHeight > 4096u) {
        rep.reason = "corrupted screenshot dimensions";
        return rep;
    }
    const uint64_t bpp       = (rep.version >= 12u) ? 4ull : 3ull;
    const uint64_t shotBytes = uint64_t(rep.shotWidth) * uint64_t(rep.shotHeight) * bpp;
    if (shotBytes > size - headerEnd) {
        rep.reason = "truncated screenshot payload";
        return rep;
    }

    const size_t bodyOff = headerEnd + static_cast<size_t>(shotBytes);
    if (bodyOff >= size) {
        rep.reason = "missing save body after screenshot";
        return rep;
    }

    // Header & screenshot validated; start at Healthy and apply risk escalations below.
    rep.health = SaveHealthLevel::Healthy;

    // Parse body (uncompressed or LZ4-compressed SSE v12) for Papyrus string-table count
    if (rep.version >= 12u && rep.compressionType == 2u) {
        if (size - bodyOff < 8u) {
            EscalateHealth(rep.health, SaveHealthLevel::Corrupt, rep.reason, "truncated LZ4 size header");
            return rep;
        }
        std::memcpy(&rep.uncompressedBodyBytes, data + bodyOff, 4u);
        std::memcpy(&rep.compressedBodyBytes,   data + bodyOff + 4u, 4u);
        if (rep.compressedBodyBytes == 0u || rep.uncompressedBodyBytes == 0u ||
            rep.uncompressedBodyBytes > kMaxLz4DecodeBytes ||
            size_t(rep.compressedBodyBytes) > size - (bodyOff + 8u)) {
            EscalateHealth(rep.health, SaveHealthLevel::Corrupt, rep.reason, "corrupt/truncated LZ4 payload");
            return rep;
        }
        std::vector<uint8_t> decoded(rep.uncompressedBodyBytes);
        if (!DecompressEssLz4Block(data + bodyOff + 8u, rep.compressedBodyBytes,
                                   decoded.data(), decoded.size())) {
            EscalateHealth(rep.health, SaveHealthLevel::Corrupt, rep.reason, "LZ4 decompression failed");
            return rep;
        }
        AuditUncompressedEssBody(decoded.data(), decoded.size(), bodyOff, rep);
    } else if (rep.compressionType == 0u) {
        rep.uncompressedBodyBytes = static_cast<uint32_t>(size - bodyOff);
        rep.compressedBodyBytes   = rep.uncompressedBodyBytes;
        AuditUncompressedEssBody(data + bodyOff, size - bodyOff, bodyOff, rep);
    } else {
        // compressionType == 1 (legacy zlib): validate size header cleanly
        if (size - bodyOff < 8u) {
            EscalateHealth(rep.health, SaveHealthLevel::Corrupt, rep.reason, "truncated zlib size header");
            return rep;
        }
        std::memcpy(&rep.uncompressedBodyBytes, data + bodyOff, 4u);
        std::memcpy(&rep.compressedBodyBytes,   data + bodyOff + 4u, 4u);
        if (size_t(rep.compressedBodyBytes) > size - (bodyOff + 8u)) {
            EscalateHealth(rep.health, SaveHealthLevel::Corrupt, rep.reason, "truncated zlib payload");
            return rep;
        }
    }

    // Evaluate Papyrus 65,535 string-table overflow risk
    if (rep.papyrusAudited) {
        if (rep.papyrusStringCount >= kStringCorruptCeiling) {
            EscalateHealth(rep.health, SaveHealthLevel::Corrupt, rep.reason,
                           "Papyrus string-table overflow (" + std::to_string(rep.papyrusStringCount) + "/65535)");
        } else if (rep.papyrusStringCount >= kStringCritThreshold) {
            EscalateHealth(rep.health, SaveHealthLevel::Critical, rep.reason,
                           "Papyrus string-table critical (" + std::to_string(rep.papyrusStringCount) + "/65535)");
        } else if (rep.papyrusStringCount > kStringWarnThreshold) {
            EscalateHealth(rep.health, SaveHealthLevel::Warning, rep.reason,
                           "Papyrus string-table elevated (" + std::to_string(rep.papyrusStringCount) + "/65535)");
        }
    }

    // Evaluate save file bloat thresholds (>32 MB Warning, >64 MB Critical)
    if (size > kSizeCritBytes) {
        EscalateHealth(rep.health, SaveHealthLevel::Critical, rep.reason, "save file bloat > 64 MB");
    } else if (size > kSizeWarnBytes) {
        EscalateHealth(rep.health, SaveHealthLevel::Warning, rep.reason, "save file size > 32 MB");
    }

    if (rep.reason.empty()) {
        rep.reason = "OK";
    }
    return rep;
}

EssSaveAuditReport AuditEssSaveFile(const std::string& filePath) {
    EssSaveAuditReport rep{};
    rep.filePath = filePath;
    try {
        const std::filesystem::path p(filePath);
        rep.fileName = p.filename().string();
        std::ifstream ifs(p, std::ios::binary | std::ios::ate);
        if (!ifs.is_open()) {
            rep.health = SaveHealthLevel::Corrupt;
            rep.reason = "unable to open file";
            return rep;
        }
        const std::streamoff fsize = ifs.tellg();
        if (fsize <= 0 || fsize > 256ll * 1024ll * 1024ll) {
            rep.health = SaveHealthLevel::Corrupt;
            rep.reason = "invalid file size";
            return rep;
        }
        std::vector<uint8_t> buf(static_cast<size_t>(fsize));
        ifs.seekg(0, std::ios::beg);
        if (!ifs.read(reinterpret_cast<char*>(buf.data()), fsize)) {
            rep.health = SaveHealthLevel::Corrupt;
            rep.reason = "read error";
            return rep;
        }
        rep = AuditEssSaveBytes(buf.data(), buf.size(), rep.fileName);
        rep.filePath = filePath;
    } catch (...) {
        rep.health = SaveHealthLevel::Corrupt;
        rep.reason = "filesystem exception";
    }
    return rep;
}

std::vector<EssSaveAuditReport> AuditSkyrimSavesDirectory(const std::string& savesDir, size_t maxNewestFiles) {
    std::vector<EssSaveAuditReport> results;
    if (savesDir.empty() || maxNewestFiles == 0u) return results;

    try {
        std::error_code ec;
        if (!std::filesystem::exists(savesDir, ec) || !std::filesystem::is_directory(savesDir, ec)) {
            return results;
        }

        struct Cand {
            std::filesystem::path           path;
            std::filesystem::file_time_type mtime;
        };
        std::vector<Cand> cands;
        for (const auto& entry : std::filesystem::directory_iterator(savesDir, ec)) {
            if (ec) break;
            if (!entry.is_regular_file(ec)) continue;
            std::string ext = entry.path().extension().string();
            for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (ext != ".ess") continue;
            cands.push_back({entry.path(), entry.last_write_time(ec)});
        }

        std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) {
            return a.mtime > b.mtime; // newest first
        });

        const size_t n = std::min(cands.size(), maxNewestFiles);
        results.reserve(n);
        for (size_t i = 0u; i < n; ++i) {
            results.push_back(AuditEssSaveFile(cands[i].path.string()));
        }
    } catch (...) {
        // Non-blocking / fail-safe directory audit
    }
    return results;
}

// --- FrameHitchWatchdog ---

void FrameHitchWatchdog::Reset() noexcept {
    m_frameRing.fill(0.0f);
    m_frameCount     = 0u;
    m_frameWritePos  = 0u;
    m_hitchStampSec.fill(0.0);
    m_hitchRingCount = 0u;
    m_hitchRingWrite = 0u;
    m_elapsedSec     = 0.0;
    m_totalFrames    = 0ull;
    m_hitchCount     = 0u;
    m_worstHitchMs   = 0.0f;
    m_lastFrameMs    = 0.0f;
}

void FrameHitchWatchdog::RecordFrame(double frameMs) noexcept {
    if (!std::isfinite(frameMs) || frameMs < 0.0) return;
    // Clamp absurd suspend/alt-tab gaps (> 10 s) for rolling clock sanity while still recording hitch
    const double clampedClockMs = std::min(frameMs, 10000.0);
    m_elapsedSec += clampedClockMs * 0.001;
    ++m_totalFrames;

    const float fms = static_cast<float>(std::min(frameMs, 60000.0));
    m_lastFrameMs = fms;
    m_frameRing[m_frameWritePos] = fms;
    m_frameWritePos = (m_frameWritePos + 1u) % kFrameWindowSize;
    if (m_frameCount < kFrameWindowSize) ++m_frameCount;

    if (frameMs > kHitchThresholdMs) {
        ++m_hitchCount;
        if (fms > m_worstHitchMs) m_worstHitchMs = fms;
        m_hitchStampSec[m_hitchRingWrite] = m_elapsedSec;
        m_hitchRingWrite = (m_hitchRingWrite + 1u) % kHitchTimeRingCap;
        if (m_hitchRingCount < kHitchTimeRingCap) ++m_hitchRingCount;
    }
}

uint32_t FrameHitchWatchdog::HitchesLast60Sec() const noexcept {
    if (m_hitchRingCount == 0u) return 0u;
    const double cutoff = m_elapsedSec - 60.0;
    uint32_t cnt = 0u;
    for (size_t i = 0u; i < m_hitchRingCount; ++i) {
        if (m_hitchStampSec[i] >= cutoff) ++cnt;
    }
    return cnt;
}

float FrameHitchWatchdog::P95FrameMs() const noexcept {
    if (m_frameCount == 0u) return 0.0f;
    std::array<float, kFrameWindowSize> sorted{};
    for (size_t i = 0u; i < m_frameCount; ++i) {
        sorted[i] = m_frameRing[i];
    }
    std::sort(sorted.begin(), sorted.begin() + static_cast<ptrdiff_t>(m_frameCount));
    const size_t idx = std::min(m_frameCount - 1u, (m_frameCount * 95u) / 100u);
    return sorted[idx];
}

// --- Runtime Lifecycle & C ABI ---

std::string SavePapyrusGuardInit(const std::string& overrideSavesDir) {
    const std::string dir = overrideSavesDir.empty() ? DefaultSkyrimSavesDir() : overrideSavesDir;
    const std::vector<EssSaveAuditReport> reports = AuditSkyrimSavesDirectory(dir, 8u);

    uint8_t worstHealth = 0u;
    uint32_t worstStrings = 0u;
    float worstMb = 0.0f;
    std::string worstFile;
    std::string worstReason;

    for (const auto& r : reports) {
        if (static_cast<uint8_t>(r.health) > worstHealth) {
            worstHealth = static_cast<uint8_t>(r.health);
            worstFile   = r.fileName;
            worstReason = r.reason;
        }
        if (r.papyrusStringCount > worstStrings) worstStrings = r.papyrusStringCount;
        const float mb = float(double(r.fileSizeBytes) / (1024.0 * 1024.0));
        if (mb > worstMb) worstMb = mb;
    }

    {
        std::lock_guard<std::mutex> lk(g_guard.mu);
        g_guard.initialized      = true;
        g_guard.savesAudited     = static_cast<uint16_t>(std::min<size_t>(reports.size(), 65535u));
        g_guard.worstSaveHealth  = worstHealth;
        g_guard.worstStringCount = worstStrings;
        g_guard.worstSaveSizeMb  = worstMb;
        g_guard.watchdog.Reset();
    }

    char buf[320];
    if (reports.empty()) {
        std::snprintf(buf, sizeof(buf),
                      "[SaveGuard] initialized (0 .ess files found in %s; Papyrus hitch watchdog >50ms armed)",
                      dir.empty() ? "<none>" : dir.c_str());
    } else if (worstHealth > 0u) {
        std::snprintf(buf, sizeof(buf),
                      "[SaveGuard] audited %zu saves | WORST=%s (%s: %s) | maxStrings=%u/65535 maxSize=%.2fMB",
                      reports.size(), SaveHealthLevelName(static_cast<SaveHealthLevel>(worstHealth)),
                      worstFile.c_str(), worstReason.c_str(), worstStrings, double(worstMb));
    } else {
        std::snprintf(buf, sizeof(buf),
                      "[SaveGuard] audited %zu saves | ALL Healthy | maxStrings=%u/65535 maxSize=%.2fMB | hitch watchdog >50ms armed",
                      reports.size(), worstStrings, double(worstMb));
    }
    return buf;
}

void SavePapyrusGuardOnPresent(double frameMs) {
    if (!GetLiveConfig().savePapyrusGuardEnabled) return;
    std::lock_guard<std::mutex> lk(g_guard.mu);
    if (!g_guard.initialized) {
        g_guard.initialized = true;
    }
    g_guard.watchdog.RecordFrame(frameMs);
}

void SavePapyrusGuardShutdown() {
    std::lock_guard<std::mutex> lk(g_guard.mu);
    g_guard.initialized = false;
    g_guard.watchdog.Reset();
}

}  // namespace raw::rt

extern "C" __declspec(dllexport) int RAW_GetSavePapyrusHealth(raw::rt::RawSavePapyrusHealth* out) {
    if (!out) return 0;
    const raw::rt::LiveEngineConfig cfg = raw::rt::GetLiveConfig();
    std::lock_guard<std::mutex> lk(raw::rt::g_guard.mu);
    out->guardEnabled     = cfg.savePapyrusGuardEnabled ? 1u : 0u;
    out->worstSaveHealth  = raw::rt::g_guard.worstSaveHealth;
    out->savesAudited     = raw::rt::g_guard.savesAudited;
    out->worstStringCount = raw::rt::g_guard.worstStringCount;
    out->worstSaveSizeMb  = raw::rt::g_guard.worstSaveSizeMb;
    out->hitchCount       = raw::rt::g_guard.watchdog.HitchCount();
    out->hitchesLast60Sec = raw::rt::g_guard.watchdog.HitchesLast60Sec();
    out->worstHitchMs     = raw::rt::g_guard.watchdog.WorstHitchMs();
    out->p95FrameMs       = raw::rt::g_guard.watchdog.P95FrameMs();
    out->lastFrameMs      = raw::rt::g_guard.watchdog.LastFrameMs();
    out->totalFrames      = raw::rt::g_guard.watchdog.TotalFrames();
    return raw::rt::g_guard.initialized ? 1 : 0;
}
