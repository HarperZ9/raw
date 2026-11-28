#pragma once
// rt_crash_forensics.h: Wave 47 Vectored Exception Handler (VEH) Crash-Dump Forensics &
// 7,376-Class PE64 RTTI Register/Stack Symbolizer (`raw::rt`).
// Zero external dependencies beyond optional Zydis when linked into raw_engine.dll.
// Allocation-free crash-time lookup & formatting, flushing via Win32 CreateFileA/WriteFile.
// Original work, MIT.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace raw::rt {

struct Image;

// Flat, contiguous, sorted (vtableRva -> className/subsystem) index for O(log N) allocation-free
// crash-time RTTI symbolization loaded from raw_versions\skyrimse-1.7.104-rtti.tsv.
class RttiVtableIndex {
public:
    struct Entry {
        uint32_t vtableRva       = 0u;
        uint32_t nameOffset      = 0u;
        uint16_t nameLen         = 0u;
        uint16_t subsystemLen    = 0u;
        uint32_t subsystemOffset = 0u;
    };

    void Clear() noexcept;
    bool LoadFromFile(const std::string& tsvPath);
    bool LoadFromText(const std::string& tsvContent);

    [[nodiscard]] size_t Count() const noexcept { return m_entries.size(); }
    [[nodiscard]] bool   Empty() const noexcept { return m_entries.empty(); }

    // Allocation-free O(log N) binary search by SkyrimSE.exe vtable RVA.
    // Returns NUL-terminated class name pointer into contiguous pool, or nullptr if not found.
    [[nodiscard]] const char* LookupByRva(uint32_t vtableRva,
                                          const char** outSubsystem = nullptr) const noexcept;

    // Helper lookup by exact class name (for diagnostics & unit tests). Returns 0 if not found.
    [[nodiscard]] uint32_t LookupRvaByClassName(const char* className) const noexcept;

private:
    std::vector<Entry> m_entries;
    std::vector<char>  m_pool;
};

struct CrashRegisterSnapshot {
    // Canonical x64 order: RAX, RCX, RDX, RBX, RSP, RBP, RSI, RDI, R8..R15
    uint64_t gpr[16]{};
    uint64_t rip    = 0ull;
    uint32_t eflags = 0u;
};

struct CrashRttiHit {
    char     source[24]{};    // e.g. "RCX", "RDI", "[RSP+0x28]"
    uint64_t objectPtr = 0ull;
    uint32_t vtableRva = 0u;
    char     className[96]{};
    char     subsystem[32]{};
};

struct CrashReportInput {
    uint32_t              exceptionCode          = 0u;
    uint64_t              exceptionAddress       = 0ull;
    uint64_t              accessViolationAddress = 0ull;
    uint32_t              accessViolationOp      = 0u; // 0=READ, 1=WRITE, 8=EXEC
    uint64_t              exeBase                = 0ull;
    uint32_t              exeSize                = 0u;
    char                  exeName[64]            = "SkyrimSE.exe";
    char                  faultModuleName[64]{};
    uint64_t              faultModuleBase        = 0ull;
    CrashRegisterSnapshot regs{};
    uint8_t               faultBytes[16]{};
    uint32_t              faultBytesLen          = 0u;
    char                  decodedInstruction[128]{};
    uint64_t              stackFrames[16]{};
    uint32_t              stackFrameCount        = 0u;
    CrashRttiHit          rttiHits[32]{};
    uint32_t              rttiHitCount           = 0u;

    // Live RAW telemetry snapshot (zero-alloc fields)
    uint64_t              frameCounter           = 0ull;
    uint32_t              activeDetours          = 0u;
    uint32_t              cellFormId             = 0u;
    uint8_t               interior               = 0u;
    uint8_t               acousticEnvClass       = 0u;
    float                 windSpeed              = 0.0f;
    float                 synopticRain           = 0.0f;
    float                 synopticSnow           = 0.0f;
    uint32_t              rttiCatalogCount       = 0u;
};

struct CrashForensicsTelemetry {
    bool     initialized      = false;
    bool     vehInstalled     = false;
    bool     enabled          = false;
    uint32_t rttiClassesLoaded = 0u;
    uint32_t dumpsWritten     = 0u;
    uint64_t frameCounter     = 0ull;
};

// Strict fatal hardware exception filter: returns true ONLY for ACCESS_VIOLATION,
// ILLEGAL_INSTRUCTION, INT_DIVIDE_BY_ZERO, STACK_OVERFLOW, and PRIV_INSTRUCTION.
// Strictly rejects C++ 0xE06D7363, DBG_PRINTEXCEPTION_C, and thread-name 0x406D1388!
bool        IsFatalCrashExceptionCode(uint32_t exceptionCode) noexcept;
const char* ExceptionCodeToName(uint32_t exceptionCode) noexcept;

// Safe memory-read callback signature used by ResolveRttiHitsFromMemory (allows pure unit tests
// without SEH and live VEH with VirtualQuery + __try/__except!).
using SafeReadU64Fn = bool (*)(uint64_t address, uint64_t* outValue, void* user) noexcept;

// Scans GPRs (RAX..R15) and stack slots for pointers whose *ptr - exeBase (or ptr - exeBase)
// matches a verified SkyrimSE.exe vtable RVA in `index`. Allocation-free!
uint32_t ResolveRttiHitsFromMemory(const RttiVtableIndex& index,
                                   uint64_t exeBase,
                                   uint32_t exeSize,
                                   const CrashRegisterSnapshot& regs,
                                   const uint64_t* stackSlots,
                                   uint32_t stackSlotCount,
                                   SafeReadU64Fn readU64,
                                   void* readUser,
                                   CrashRttiHit* outHits,
                                   uint32_t maxHits) noexcept;

// Allocation-free fixed-buffer crash report formatter (safe inside a Windows VEH crash handler!).
size_t FormatCrashReportToBuffer(const CrashReportInput& in, char* outBuf, size_t outBufCap) noexcept;

// Convenience std::string wrapper for unit tests & diagnostics.
std::string FormatCrashReport(const CrashReportInput& in);

// Thread-local SEH probe guard: prevents benign SEH-guarded memory probes from triggering crash dumps.
void CrashForensicsPushSehGuard() noexcept;
void CrashForensicsPopSehGuard() noexcept;
bool CrashForensicsIsInSehGuard() noexcept;

struct ScopedCrashForensicsSehGuard {
    ScopedCrashForensicsSehGuard() noexcept { CrashForensicsPushSehGuard(); }
    ~ScopedCrashForensicsSehGuard() noexcept { CrashForensicsPopSehGuard(); }
    ScopedCrashForensicsSehGuard(const ScopedCrashForensicsSehGuard&) = delete;
    ScopedCrashForensicsSehGuard& operator=(const ScopedCrashForensicsSehGuard&) = delete;
};

#ifndef RAW_SIM_PURE_TEST
// Live raw_engine.dll VEH crash forensics lifecycle (controlled by [Diagnostics] CrashForensics=1).
std::string CrashForensicsInit(const Image& img, const std::string& gameDir, const std::string& outDir);
bool        CrashForensicsInstall();
void        CrashForensicsUninstall();
void        CrashForensicsOnPresent();
void        CrashForensicsShutdown();
bool        CrashForensicsGetTelemetry(CrashForensicsTelemetry* out);
#endif

}  // namespace raw::rt
