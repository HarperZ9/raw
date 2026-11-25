// rt_crash_forensics.cpp: Wave 47 Vectored Exception Handler (VEH) Crash-Dump Forensics &
// 7,376-Class PE64 RTTI Register/Stack Symbolizer (`raw::rt`).
// Original work, MIT.

#include "rt_crash_forensics.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

#ifndef RAW_SIM_PURE_TEST
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "rt_image.h"
#include "rt_live_config.h"
#include "rt_patch.h"
#include "rt_view.h"
#include "rt_world_sim.h"

#if defined(__has_include)
#if __has_include(<Zydis/Zydis.h>)
#include <Zydis/Zydis.h>
#define RAW_CRASH_FORENSICS_HAS_ZYDIS 1
#endif
#endif
#endif

namespace raw::rt {
namespace {

constexpr const char* kGprNames[16] = {
    "RAX", "RCX", "RDX", "RBX", "RSP", "RBP", "RSI", "RDI",
    "R8",  "R9",  "R10", "R11", "R12", "R13", "R14", "R15"
};

thread_local uint32_t t_sehGuardDepth = 0u;

void CopyTruncated(char* dst, size_t dstCap, const char* src) noexcept {
    if (!dst || dstCap == 0u) return;
    if (!src) { dst[0] = '\0'; return; }
    size_t i = 0u;
    while (i + 1u < dstCap && src[i] != '\0') {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = '\0';
}

bool AppendFmt(char* buf, size_t cap, size_t& pos, const char* fmt, ...) noexcept {
    if (!buf || cap == 0u || pos >= cap) return false;
    va_list ap;
    va_start(ap, fmt);
    const int n = std::vsnprintf(buf + pos, cap - pos, fmt, ap);
    va_end(ap);
    if (n < 0) return false;
    const size_t avail = cap - pos - 1u;
    const size_t wrote = static_cast<size_t>(n) < avail ? static_cast<size_t>(n) : avail;
    pos += wrote;
    return true;
}

bool ParseHexU32(const char* s, size_t len, uint32_t& outVal) noexcept {
    if (!s || len == 0u) return false;
    size_t i = 0u;
    while (i < len && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
    if (i + 2u <= len && s[i] == '0' && (s[i + 1u] == 'x' || s[i + 1u] == 'X')) {
        i += 2u;
    }
    if (i >= len) return false;
    uint32_t acc = 0u;
    bool any = false;
    while (i < len) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        uint32_t digit = 0u;
        if (c >= '0' && c <= '9')      digit = c - '0';
        else if (c >= 'a' && c <= 'f') digit = 10u + (c - 'a');
        else if (c >= 'A' && c <= 'F') digit = 10u + (c - 'A');
        else if (std::isspace(c))      break;
        else                           return false;
        acc = (acc << 4u) | digit;
        any = true;
        ++i;
    }
    if (!any) return false;
    outVal = acc;
    return true;
}

}  // namespace

// --- RttiVtableIndex ---

void RttiVtableIndex::Clear() noexcept {
    m_entries.clear();
    m_pool.clear();
}

bool RttiVtableIndex::LoadFromFile(const std::string& tsvPath) {
    Clear();
    if (tsvPath.empty()) return false;
    std::ifstream ifs(tsvPath, std::ios::binary);
    if (!ifs.is_open()) return false;
    std::ostringstream ss;
    ss << ifs.rdbuf();
    return LoadFromText(ss.str());
}

bool RttiVtableIndex::LoadFromText(const std::string& tsvContent) {
    Clear();
    if (tsvContent.empty()) return false;

    m_entries.reserve(8192u);
    m_pool.reserve(tsvContent.size() / 2u + 1024u);

    const char* data = tsvContent.data();
    const size_t total = tsvContent.size();
    size_t pos = 0u;

    while (pos < total) {
        size_t eol = pos;
        while (eol < total && data[eol] != '\n' && data[eol] != '\r') ++eol;
        const size_t lineLen = eol - pos;
        const char* line = data + pos;

        pos = eol;
        while (pos < total && (data[pos] == '\n' || data[pos] == '\r')) ++pos;

        if (lineLen == 0u || line[0] == '#') continue;

        // Columns: 0=class_name, 1=vtable_rva, 2=col_rva, 3=method_count, 4=first_methods, 5=inheritance, 6=subsystem
        const char* colPtr[7]{};
        size_t      colLen[7]{};
        size_t      colIdx = 0u;
        size_t      cStart = 0u;
        for (size_t i = 0u; i <= lineLen && colIdx < 7u; ++i) {
            if (i == lineLen || line[i] == '\t') {
                colPtr[colIdx] = line + cStart;
                colLen[colIdx] = i - cStart;
                ++colIdx;
                cStart = i + 1u;
            }
        }
        if (colIdx < 2u || colLen[0] == 0u || colLen[1] == 0u) continue;
        // Skip header row ("class_name\tvtable_rva...")
        if (colLen[0] == 10u && std::memcmp(colPtr[0], "class_name", 10u) == 0) continue;

        uint32_t vtableRva = 0u;
        if (!ParseHexU32(colPtr[1], colLen[1], vtableRva) || vtableRva == 0u) continue;

        Entry e{};
        e.vtableRva  = vtableRva;
        e.nameOffset = static_cast<uint32_t>(m_pool.size());
        e.nameLen    = static_cast<uint16_t>(std::min<size_t>(colLen[0], 65530u));
        m_pool.insert(m_pool.end(), colPtr[0], colPtr[0] + e.nameLen);
        m_pool.push_back('\0');

        if (colIdx >= 7u && colLen[6] > 0u) {
            e.subsystemOffset = static_cast<uint32_t>(m_pool.size());
            e.subsystemLen    = static_cast<uint16_t>(std::min<size_t>(colLen[6], 128u));
            m_pool.insert(m_pool.end(), colPtr[6], colPtr[6] + e.subsystemLen);
            m_pool.push_back('\0');
        }
        m_entries.push_back(e);
    }

    if (m_entries.empty()) return false;

    std::sort(m_entries.begin(), m_entries.end(), [](const Entry& a, const Entry& b) {
        return a.vtableRva < b.vtableRva;
    });
    // Deduplicate identical vtable RVAs keeping first sorted entry
    m_entries.erase(std::unique(m_entries.begin(), m_entries.end(), [](const Entry& a, const Entry& b) {
        return a.vtableRva == b.vtableRva;
    }), m_entries.end());
    return true;
}

const char* RttiVtableIndex::LookupByRva(uint32_t vtableRva, const char** outSubsystem) const noexcept {
    if (outSubsystem) *outSubsystem = nullptr;
    if (vtableRva == 0u || m_entries.empty()) return nullptr;

    auto it = std::lower_bound(m_entries.begin(), m_entries.end(), vtableRva,
                               [](const Entry& e, uint32_t rva) { return e.vtableRva < rva; });
    if (it == m_entries.end() || it->vtableRva != vtableRva) return nullptr;

    if (outSubsystem && it->subsystemLen > 0u && it->subsystemOffset < m_pool.size()) {
        *outSubsystem = m_pool.data() + it->subsystemOffset;
    }
    return (it->nameOffset < m_pool.size()) ? (m_pool.data() + it->nameOffset) : nullptr;
}

uint32_t RttiVtableIndex::LookupRvaByClassName(const char* className) const noexcept {
    if (!className || !*className || m_entries.empty()) return 0u;
    const size_t targetLen = std::strlen(className);
    for (const Entry& e : m_entries) {
        if (e.nameLen == targetLen && e.nameOffset < m_pool.size() &&
            std::memcmp(m_pool.data() + e.nameOffset, className, targetLen) == 0) {
            return e.vtableRva;
        }
    }
    return 0u;
}

// --- Fatal Exception Filter & SEH Guard ---

bool IsFatalCrashExceptionCode(uint32_t exceptionCode) noexcept {
    switch (exceptionCode) {
        case 0xC0000005u: // EXCEPTION_ACCESS_VIOLATION
        case 0xC000001Du: // EXCEPTION_ILLEGAL_INSTRUCTION
        case 0xC0000094u: // EXCEPTION_INT_DIVIDE_BY_ZERO
        case 0xC00000FDu: // EXCEPTION_STACK_OVERFLOW
        case 0xC0000096u: // EXCEPTION_PRIV_INSTRUCTION
            return true;
        default:
            return false;
    }
}

const char* ExceptionCodeToName(uint32_t exceptionCode) noexcept {
    switch (exceptionCode) {
        case 0xC0000005u: return "EXCEPTION_ACCESS_VIOLATION";
        case 0xC000001Du: return "EXCEPTION_ILLEGAL_INSTRUCTION";
        case 0xC0000094u: return "EXCEPTION_INT_DIVIDE_BY_ZERO";
        case 0xC00000FDu: return "EXCEPTION_STACK_OVERFLOW";
        case 0xC0000096u: return "EXCEPTION_PRIV_INSTRUCTION";
        case 0xE06D7363u: return "MSVC_CPP_EXCEPTION";
        case 0x40010006u: return "DBG_PRINTEXCEPTION_C";
        case 0x406D1388u: return "MS_VC_SET_THREAD_NAME";
        default:          return "UNKNOWN_EXCEPTION";
    }
}

void CrashForensicsPushSehGuard() noexcept { ++t_sehGuardDepth; }
void CrashForensicsPopSehGuard() noexcept  { if (t_sehGuardDepth > 0u) --t_sehGuardDepth; }
bool CrashForensicsIsInSehGuard() noexcept { return t_sehGuardDepth > 0u; }

// --- Allocation-Free RTTI Register & Stack Symbolizer ---

uint32_t ResolveRttiHitsFromMemory(const RttiVtableIndex& index,
                                   uint64_t exeBase,
                                   uint32_t exeSize,
                                   const CrashRegisterSnapshot& regs,
                                   const uint64_t* stackSlots,
                                   uint32_t stackSlotCount,
                                   SafeReadU64Fn readU64,
                                   void* readUser,
                                   CrashRttiHit* outHits,
                                   uint32_t maxHits) noexcept {
    if (index.Empty() || exeBase == 0ull || exeSize == 0u || !outHits || maxHits == 0u) {
        return 0u;
    }

    uint32_t count = 0u;
    const uint64_t exeEnd = exeBase + uint64_t(exeSize);

    auto recordHit = [&](const char* sourceLabel, uint64_t objPtr, uint32_t rva,
                         const char* clsName, const char* subsys) noexcept {
        if (count >= maxHits || !clsName) return;
        // Deduplicate identical (objectPtr, vtableRva) when scanning stack slots; keep distinct register hits!
        if (sourceLabel[0] == '[') {
            for (uint32_t i = 0u; i < count; ++i) {
                if (outHits[i].objectPtr == objPtr && outHits[i].vtableRva == rva) return;
            }
        }
        CrashRttiHit& h = outHits[count++];
        CopyTruncated(h.source, sizeof(h.source), sourceLabel);
        h.objectPtr = objPtr;
        h.vtableRva = rva;
        CopyTruncated(h.className, sizeof(h.className), clsName);
        CopyTruncated(h.subsystem, sizeof(h.subsystem), subsys ? subsys : "Unknown");
    };

    auto probeCandidate = [&](const char* label, uint64_t val) noexcept {
        if (val < 0x10000ull || count >= maxHits) return;
        // 1. Direct vtable pointer in register/slot?
        if (val >= exeBase && val < exeEnd) {
            const uint32_t rva = static_cast<uint32_t>(val - exeBase);
            const char* subsys = nullptr;
            if (const char* cls = index.LookupByRva(rva, &subsys)) {
                recordHit(label, val, rva, cls, subsys);
                return;
            }
        }
        // 2. Object instance pointer whose first QWORD (*val) is a SkyrimSE.exe vtable?
        if (readU64 && (val & 0x7ull) == 0ull) {
            uint64_t vptr = 0ull;
            if (readU64(val, &vptr, readUser) && vptr >= exeBase && vptr < exeEnd) {
                const uint32_t rva = static_cast<uint32_t>(vptr - exeBase);
                const char* subsys = nullptr;
                if (const char* cls = index.LookupByRva(rva, &subsys)) {
                    recordHit(label, val, rva, cls, subsys);
                }
            }
        }
    };

    // Scan all 16 GPRs (skip RSP at index 4 as a direct object, stack slots scanned right below)
    for (uint32_t i = 0u; i < 16u && count < maxHits; ++i) {
        if (i == 4u) continue;
        probeCandidate(kGprNames[i], regs.gpr[i]);
    }

    // Scan stack QWORD slots
    if (stackSlots && stackSlotCount > 0u) {
        const uint32_t lim = std::min<uint32_t>(stackSlotCount, 64u);
        for (uint32_t i = 0u; i < lim && count < maxHits; ++i) {
            char label[24];
            std::snprintf(label, sizeof(label), "[RSP+0x%02X]", i * 8u);
            probeCandidate(label, stackSlots[i]);
        }
    }

    return count;
}

// --- Pure Allocation-Free Report Formatter ---

size_t FormatCrashReportToBuffer(const CrashReportInput& in, char* outBuf, size_t outBufCap) noexcept {
    if (!outBuf || outBufCap == 0u) return 0u;
    outBuf[0] = '\0';
    size_t pos = 0u;

    const char* excName = ExceptionCodeToName(in.exceptionCode);
    const bool ripInExe = (in.exeBase != 0ull && in.exeSize > 0u &&
                           in.exceptionAddress >= in.exeBase &&
                           in.exceptionAddress < in.exeBase + uint64_t(in.exeSize));
    const uint64_t ripExeRva = ripInExe ? (in.exceptionAddress - in.exeBase) : 0ull;
    const bool hasFaultMod = (in.faultModuleName[0] != '\0' && in.faultModuleBase != 0ull &&
                              in.exceptionAddress >= in.faultModuleBase);

    AppendFmt(outBuf, outBufCap, pos,
              "========================================================================\n"
              "RAW ENGINE RUNTIME - VEH CRASH FORENSICS & RTTI SYMBOLIZER REPORT\n"
              "========================================================================\n");
    AppendFmt(outBuf, outBufCap, pos,
              "Exception : 0x%08X (%s)\n", in.exceptionCode, excName);

    if (in.exceptionCode == 0xC0000005u) {
        const char* opStr = (in.accessViolationOp == 1u) ? "WRITE"
                          : (in.accessViolationOp == 8u) ? "DEP_EXEC" : "READ";
        AppendFmt(outBuf, outBufCap, pos,
                  "Faulting  : %s at 0x%016llX\n",
                  opStr, static_cast<unsigned long long>(in.accessViolationAddress));
    }

    if (ripInExe) {
        AppendFmt(outBuf, outBufCap, pos,
                  "Address   : 0x%016llX (%s+0x%llX)\n",
                  static_cast<unsigned long long>(in.exceptionAddress),
                  in.exeName[0] ? in.exeName : "SkyrimSE.exe",
                  static_cast<unsigned long long>(ripExeRva));
    } else if (hasFaultMod) {
        AppendFmt(outBuf, outBufCap, pos,
                  "Address   : 0x%016llX (%s+0x%llX)\n",
                  static_cast<unsigned long long>(in.exceptionAddress),
                  in.faultModuleName,
                  static_cast<unsigned long long>(in.exceptionAddress - in.faultModuleBase));
    } else {
        AppendFmt(outBuf, outBufCap, pos,
                  "Address   : 0x%016llX (unmapped / JIT)\n",
                  static_cast<unsigned long long>(in.exceptionAddress));
    }

    AppendFmt(outBuf, outBufCap, pos,
              "ExeBase   : 0x%016llX (size=0x%X, RTTI catalog=%u classes)\n",
              static_cast<unsigned long long>(in.exeBase), in.exeSize, in.rttiCatalogCount);

    // Instruction disassembly + raw bytes
    if (in.decodedInstruction[0] != '\0') {
        AppendFmt(outBuf, outBufCap, pos, "Insn      : %s", in.decodedInstruction);
    } else {
        AppendFmt(outBuf, outBufCap, pos, "Insn      : <hex>");
    }
    if (in.faultBytesLen > 0u) {
        AppendFmt(outBuf, outBufCap, pos, "  [");
        const uint32_t nb = std::min<uint32_t>(in.faultBytesLen, 16u);
        for (uint32_t i = 0u; i < nb; ++i) {
            AppendFmt(outBuf, outBufCap, pos, i == 0u ? "%02X" : " %02X", in.faultBytes[i]);
        }
        AppendFmt(outBuf, outBufCap, pos, "]");
    }
    AppendFmt(outBuf, outBufCap, pos, "\n\n[Registers (x86-64)]\n");
    for (uint32_t i = 0u; i < 16u; i += 2u) {
        AppendFmt(outBuf, outBufCap, pos,
                  "  %-3s = 0x%016llX    %-3s = 0x%016llX\n",
                  kGprNames[i],     static_cast<unsigned long long>(in.regs.gpr[i]),
                  kGprNames[i + 1], static_cast<unsigned long long>(in.regs.gpr[i + 1]));
    }
    AppendFmt(outBuf, outBufCap, pos,
              "  RIP = 0x%016llX    EFL = 0x%08X\n\n",
              static_cast<unsigned long long>(in.regs.rip), in.regs.eflags);

    // RTTI Register / Stack Class Attribution
    AppendFmt(outBuf, outBufCap, pos, "[RTTI Register & Stack Attribution (%u hits)]\n", in.rttiHitCount);
    if (in.rttiHitCount == 0u) {
        AppendFmt(outBuf, outBufCap, pos, "  (none matched verified vtable catalog)\n");
    } else {
        const uint32_t lim = std::min<uint32_t>(in.rttiHitCount, 32u);
        for (uint32_t i = 0u; i < lim; ++i) {
            const CrashRttiHit& h = in.rttiHits[i];
            AppendFmt(outBuf, outBufCap, pos,
                      "  %-10s -> 0x%016llX : %s (vtable +0x%X, %s)\n",
                      h.source, static_cast<unsigned long long>(h.objectPtr),
                      h.className, h.vtableRva, h.subsystem);
        }
    }

    // Stack Backtrace RVAs
    AppendFmt(outBuf, outBufCap, pos, "\n[Call Stack (%u frames)]\n", in.stackFrameCount);
    const uint32_t nf = std::min<uint32_t>(in.stackFrameCount, 16u);
    for (uint32_t i = 0u; i < nf; ++i) {
        const uint64_t addr = in.stackFrames[i];
        if (in.exeBase != 0ull && in.exeSize > 0u &&
            addr >= in.exeBase && addr < in.exeBase + uint64_t(in.exeSize)) {
            AppendFmt(outBuf, outBufCap, pos,
                      "  #%02u  0x%016llX  %s+0x%llX\n",
                      i, static_cast<unsigned long long>(addr),
                      in.exeName[0] ? in.exeName : "SkyrimSE.exe",
                      static_cast<unsigned long long>(addr - in.exeBase));
        } else {
            AppendFmt(outBuf, outBufCap, pos,
                      "  #%02u  0x%016llX\n", i, static_cast<unsigned long long>(addr));
        }
    }

    // Live RAW Telemetry Snapshot
    AppendFmt(outBuf, outBufCap, pos,
              "\n[RAW Live Runtime Telemetry]\n"
              "  Frame=%llu | ActiveDetours=%u | Cell=0x%08X (interior=%u)\n"
              "  Weather: wind=%.1f m/s rain=%.2f snow=%.2f acousticEnv=%u\n",
              static_cast<unsigned long long>(in.frameCounter),
              in.activeDetours, in.cellFormId, in.interior,
              static_cast<double>(in.windSpeed),
              static_cast<double>(in.synopticRain),
              static_cast<double>(in.synopticSnow),
              in.acousticEnvClass);

    return pos;
}

std::string FormatCrashReport(const CrashReportInput& in) {
    char buf[16384];
    const size_t n = FormatCrashReportToBuffer(in, buf, sizeof(buf));
    return std::string(buf, n);
}

// --- Live Windows VEH Crash Handler (excluded under RAW_SIM_PURE_TEST) ---

#ifndef RAW_SIM_PURE_TEST
namespace {

RttiVtableIndex       g_rttiIndex;
std::atomic<bool>     g_initialized{false};
std::atomic<bool>     g_enabled{false};
std::atomic<bool>     g_inHandler{false};
std::atomic<uint32_t> g_dumpsWritten{0u};
std::atomic<uint64_t> g_frameCounter{0ull};
PVOID                 g_vehHandle = nullptr;
uint64_t              g_exeBase   = 0ull;
uint32_t              g_exeSize   = 0u;
char                  g_logPath[MAX_PATH]{};
char                  g_reportBuf[16384]{};

bool SafeReadMemoryBytesWin(uint64_t addr, void* dst, size_t len) noexcept {
    if (addr < 0x10000ull || !dst || len == 0u) return false;
    MEMORY_BASIC_INFORMATION mbi{};
    if (VirtualQuery(reinterpret_cast<LPCVOID>(addr), &mbi, sizeof(mbi)) == 0u) return false;
    if (mbi.State != MEM_COMMIT) return false;
    const DWORD prot = mbi.Protect & 0xFFu;
    const bool readable = (prot == PAGE_READONLY || prot == PAGE_READWRITE ||
                           prot == PAGE_WRITECOPY || prot == PAGE_EXECUTE_READ ||
                           prot == PAGE_EXECUTE_READWRITE || prot == PAGE_EXECUTE_WRITECOPY);
    if (!readable || (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
    const uintptr_t regionEnd = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
    if (addr + len > regionEnd) return false;

    __try {
        std::memcpy(dst, reinterpret_cast<const void*>(addr), len);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool SafeReadU64Win(uint64_t addr, uint64_t* outVal, void*) noexcept {
    return SafeReadMemoryBytesWin(addr, outVal, sizeof(uint64_t));
}

void DecodeFaultInstruction(uint64_t rip, const uint8_t* bytes, uint32_t len, char* outStr, size_t outCap) noexcept {
    if (!outStr || outCap == 0u) return;
    outStr[0] = '\0';
#if defined(RAW_CRASH_FORENSICS_HAS_ZYDIS)
    if (bytes && len > 0u) {
        ZydisDisassembledInstruction insn{};
        if (ZYAN_SUCCESS(ZydisDisassembleIntel(ZYDIS_MACHINE_MODE_LONG_64, rip, bytes, len, &insn))) {
            CopyTruncated(outStr, outCap, insn.text);
            return;
        }
    }
#else
    (void)rip;
    (void)bytes;
    (void)len;
#endif
    CopyTruncated(outStr, outCap, "<undecoded>");
}

LONG CALLBACK RawVehCrashHandler(PEXCEPTION_POINTERS ep) {
    if (!ep || !ep->ExceptionRecord || !ep->ContextRecord) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    const DWORD code = ep->ExceptionRecord->ExceptionCode;
    if (!IsFatalCrashExceptionCode(code)) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    if (!g_enabled.load(std::memory_order_acquire) || CrashForensicsIsInSehGuard()) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    if (g_inHandler.exchange(true, std::memory_order_acq_rel)) {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    CrashReportInput in{};
    in.exceptionCode    = code;
    in.exceptionAddress = reinterpret_cast<uint64_t>(ep->ExceptionRecord->ExceptionAddress);
    if (code == EXCEPTION_ACCESS_VIOLATION && ep->ExceptionRecord->NumberParameters >= 2u) {
        in.accessViolationOp      = static_cast<uint32_t>(ep->ExceptionRecord->ExceptionInformation[0]);
        in.accessViolationAddress = static_cast<uint64_t>(ep->ExceptionRecord->ExceptionInformation[1]);
    }
    in.exeBase          = g_exeBase;
    in.exeSize          = g_exeSize;
    in.rttiCatalogCount = static_cast<uint32_t>(g_rttiIndex.Count());
    in.frameCounter     = g_frameCounter.load(std::memory_order_relaxed);
    in.activeDetours    = static_cast<uint32_t>(PatchCount());

    // Resolve faulting module name if outside SkyrimSE.exe
    HMODULE faultMod = nullptr;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(in.exceptionAddress), &faultMod) && faultMod) {
        in.faultModuleBase = reinterpret_cast<uint64_t>(faultMod);
        char modPath[MAX_PATH]{};
        if (GetModuleFileNameA(faultMod, modPath, MAX_PATH) > 0u) {
            const char* slash = std::strrchr(modPath, '\\');
            CopyTruncated(in.faultModuleName, sizeof(in.faultModuleName), slash ? (slash + 1) : modPath);
        }
    }

    const CONTEXT& ctx = *ep->ContextRecord;
    in.regs.gpr[0]  = ctx.Rax; in.regs.gpr[1]  = ctx.Rcx; in.regs.gpr[2]  = ctx.Rdx; in.regs.gpr[3]  = ctx.Rbx;
    in.regs.gpr[4]  = ctx.Rsp; in.regs.gpr[5]  = ctx.Rbp; in.regs.gpr[6]  = ctx.Rsi; in.regs.gpr[7]  = ctx.Rdi;
    in.regs.gpr[8]  = ctx.R8;  in.regs.gpr[9]  = ctx.R9;  in.regs.gpr[10] = ctx.R10; in.regs.gpr[11] = ctx.R11;
    in.regs.gpr[12] = ctx.R12; in.regs.gpr[13] = ctx.R13; in.regs.gpr[14] = ctx.R14; in.regs.gpr[15] = ctx.R15;
    in.regs.rip     = ctx.Rip;
    in.regs.eflags  = ctx.EFlags;

    // Safely read up to 16 faulting instruction bytes & decode via Zydis
    if (SafeReadMemoryBytesWin(ctx.Rip, in.faultBytes, sizeof(in.faultBytes))) {
        in.faultBytesLen = sizeof(in.faultBytes);
        DecodeFaultInstruction(ctx.Rip, in.faultBytes, in.faultBytesLen,
                               in.decodedInstruction, sizeof(in.decodedInstruction));
    }

    // Safely read 64 stack QWORDs for RTTI object attribution + heuristic return-address capture
    uint64_t stackSlots[64]{};
    uint32_t validStackSlots = 0u;
    for (uint32_t i = 0u; i < 64u; ++i) {
        if (!SafeReadU64Win(ctx.Rsp + uint64_t(i) * 8ull, &stackSlots[i], nullptr)) break;
        ++validStackSlots;
    }

    // Capture native Windows stack backtrace
    void* btPtrs[16]{};
    const USHORT captured = RtlCaptureStackBackTrace(0u, 16u, btPtrs, nullptr);
    in.stackFrameCount = std::min<uint32_t>(captured, 16u);
    for (uint32_t i = 0u; i < in.stackFrameCount; ++i) {
        in.stackFrames[i] = reinterpret_cast<uint64_t>(btPtrs[i]);
    }

    // Allocation-free O(log N) 7,376-class RTTI attribution across registers + stack!
    in.rttiHitCount = ResolveRttiHitsFromMemory(
        g_rttiIndex, g_exeBase, g_exeSize, in.regs,
        stackSlots, validStackSlots, &SafeReadU64Win, nullptr,
        in.rttiHits, 32u);

    // Best-effort safe live telemetry snapshot (guarded so never crashes inside handler)
    __try {
        EngineView ev{};
        if (RAW_EngineView(&ev) > 0u) {
            in.cellFormId = ev.cellFormId;
            in.interior   = static_cast<uint8_t>(ev.interior != 0u ? 1u : 0u);
        }
        RawWorldSimTelemetry ws{};
        if (RAW_GetWorldSimTelemetry(&ws)) {
            in.acousticEnvClass = ws.acousticEnvClass;
            in.windSpeed        = ws.windSpeed;
            in.synopticRain     = ws.synopticRain;
            in.synopticSnow     = ws.synopticSnow;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        // Never fail crash dump if telemetry memory is corrupted
    }

    const size_t reportLen = FormatCrashReportToBuffer(in, g_reportBuf, sizeof(g_reportBuf));
    if (reportLen > 0u && g_logPath[0] != '\0') {
        HANDLE hf = CreateFileA(g_logPath, GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hf != INVALID_HANDLE_VALUE) {
            DWORD written = 0u;
            WriteFile(hf, g_reportBuf, static_cast<DWORD>(reportLen), &written, nullptr);
            FlushFileBuffers(hf);
            CloseHandle(hf);
            g_dumpsWritten.fetch_add(1u, std::memory_order_relaxed);
        }
    }

    g_inHandler.store(false, std::memory_order_release);
    // NEVER swallow a real crash!
    return EXCEPTION_CONTINUE_SEARCH;
}

}  // namespace

std::string CrashForensicsInit(const Image& img, const std::string& gameDir, const std::string& outDir) {
    CrashForensicsShutdown();

    g_exeBase = reinterpret_cast<uint64_t>(img.base);
    g_exeSize = 0u;
    if (img.base) {
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(img.base);
        const auto* nt  = reinterpret_cast<const IMAGE_NT_HEADERS64*>(img.base + dos->e_lfanew);
        g_exeSize = nt->OptionalHeader.SizeOfImage;
    }

    std::string logFile = outDir + "\\crash_forensics_latest.log";
    CopyTruncated(g_logPath, sizeof(g_logPath), logFile.c_str());

    // Load Wave 38 7,376-class PE64 RTTI vtable catalog
    bool loaded = g_rttiIndex.LoadFromFile(gameDir + "raw_versions\\skyrimse-1.7.104-rtti.tsv");
    if (!loaded) {
        loaded = g_rttiIndex.LoadFromFile(gameDir + "data\\versions\\skyrimse-1.7.104-rtti.tsv");
    }

    g_frameCounter.store(0ull, std::memory_order_relaxed);
    g_dumpsWritten.store(0u, std::memory_order_relaxed);
    g_initialized.store(true, std::memory_order_release);

    const LiveEngineConfig cfg = GetLiveConfig();
    bool installed = false;
    if (cfg.crashForensicsEnabled) {
        installed = CrashForensicsInstall();
    }

    char buf[256];
    std::snprintf(buf, sizeof(buf),
                  "[CrashForensics] initialized: VEH=%s, rttiClasses=%zu, log=%s",
                  installed ? "ARMED" : "disabled", g_rttiIndex.Count(), g_logPath);
    return buf;
}

bool CrashForensicsInstall() {
    if (g_vehHandle != nullptr) {
        g_enabled.store(true, std::memory_order_release);
        return true;
    }
    // First=0 so debugger / early handlers observe first; handler always returns EXCEPTION_CONTINUE_SEARCH.
    g_vehHandle = AddVectoredExceptionHandler(0u, &RawVehCrashHandler);
    const bool ok = (g_vehHandle != nullptr);
    g_enabled.store(ok, std::memory_order_release);
    return ok;
}

void CrashForensicsUninstall() {
    g_enabled.store(false, std::memory_order_release);
    if (g_vehHandle != nullptr) {
        RemoveVectoredExceptionHandler(g_vehHandle);
        g_vehHandle = nullptr;
    }
}

void CrashForensicsOnPresent() {
    if (!g_initialized.load(std::memory_order_acquire)) return;
    const uint64_t f = g_frameCounter.fetch_add(1ull, std::memory_order_relaxed) + 1ull;
    // Sync hot-reloaded [Diagnostics] CrashForensics=0/1 every 60 frames
    if ((f % 60ull) == 0ull) {
        const bool want = GetLiveConfig().crashForensicsEnabled;
        const bool have = g_enabled.load(std::memory_order_relaxed);
        if (want && !have) {
            CrashForensicsInstall();
        } else if (!want && have) {
            CrashForensicsUninstall();
        }
    }
}

void CrashForensicsShutdown() {
    CrashForensicsUninstall();
    g_initialized.store(false, std::memory_order_release);
    g_rttiIndex.Clear();
}

bool CrashForensicsGetTelemetry(CrashForensicsTelemetry* out) {
    if (!out) return false;
    out->initialized       = g_initialized.load(std::memory_order_acquire);
    out->vehInstalled      = (g_vehHandle != nullptr);
    out->enabled           = g_enabled.load(std::memory_order_acquire);
    out->rttiClassesLoaded = static_cast<uint32_t>(g_rttiIndex.Count());
    out->dumpsWritten      = g_dumpsWritten.load(std::memory_order_relaxed);
    out->frameCounter      = g_frameCounter.load(std::memory_order_relaxed);
    return out->initialized;
}
#endif

}  // namespace raw::rt
