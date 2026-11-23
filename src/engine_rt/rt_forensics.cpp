// Deep Engine Crash Forensics, Flight Recorder & Proactive RAM-Pressure Guard (RAW engine runtime).
// Original work, MIT.
#include "rt_forensics.h"
#include "rt_view.h"

#include <windows.h>
#include <psapi.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>

extern "C" uint64_t RAW_EngineView(raw::rt::EngineView* out);

namespace raw::rt {
namespace {

struct FlightEvent {
    uint64_t tickMs = 0;
    uint32_t tid = 0;
    char tag[28] = {};
    uint64_t a = 0, b = 0;
};

constexpr uint32_t kRingSize = 256;
FlightEvent g_ring[kRingSize]{};
std::atomic<uint32_t> g_ringHead{0};
std::string g_outDir;
PVOID g_veh = nullptr;
std::atomic<bool> g_written{false};
uint32_t g_presentCount = 0;
uint32_t g_lastMemLoadPct = 0;
uint64_t g_lastAvailPhysMB = 0;
uint64_t g_lastWorkingSetMB = 0;
uint32_t g_ramPressureEvents = 0;

const char* ClassifyFault(uintptr_t faultAddr, uint32_t memLoadPct, uint64_t availPhysMB) {
    if (memLoadPct >= 95u || availPhysMB < 1024ull) return "RAM_EXHAUSTION_OOM";
    if (faultAddr < 0x10000ull) return "NULL_DEREF";
    if (faultAddr >= 0x00007FFFFFFFFFFFull) return "NON_CANONICAL_PTR";
    if ((faultAddr & 0xFFFF00000000ull) == 0ull || (faultAddr & 0xFFFFFFFFull) <= 0xFFFFull) {
        return "UNINIT_OR_TRUNCATED_PTR";
    }
    MEMORY_BASIC_INFORMATION mbi{};
    if (VirtualQuery(reinterpret_cast<LPCVOID>(faultAddr), &mbi, sizeof mbi)) {
        if (mbi.State == MEM_FREE) return "USE_AFTER_FREE_OR_UNMAPPED";
        if (mbi.Protect == PAGE_NOACCESS) return "GUARD_OR_NOACCESS_PAGE";
    }
    return "INVALID_MEMORY_ACCESS";
}

LONG CALLBACK ForensicsVeh(EXCEPTION_POINTERS* ep) {
    if (!ep || !ep->ExceptionRecord) return EXCEPTION_CONTINUE_SEARCH;
    const DWORD code = ep->ExceptionRecord->ExceptionCode;
    if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_ILLEGAL_INSTRUCTION &&
        code != EXCEPTION_STACK_OVERFLOW && code != EXCEPTION_IN_PAGE_ERROR) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    if (g_written.exchange(true)) return EXCEPTION_CONTINUE_SEARCH;

    MEMORYSTATUSEX ms{}; ms.dwLength = sizeof ms;
    GlobalMemoryStatusEx(&ms);
    PROCESS_MEMORY_COUNTERS_EX pmc{}; pmc.cb = sizeof pmc;
    GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof pmc);
    const uint64_t availPhysMB = ms.ullAvailPhys >> 20;
    const uint64_t wsMB = pmc.WorkingSetSize >> 20;
    const uintptr_t rip = reinterpret_cast<uintptr_t>(ep->ExceptionRecord->ExceptionAddress);
    const uintptr_t faultAddr = ep->ExceptionRecord->NumberParameters >= 2
                                    ? static_cast<uintptr_t>(ep->ExceptionRecord->ExceptionInformation[1])
                                    : 0;
    const char* cls = ClassifyFault(faultAddr, ms.dwMemoryLoad, availPhysMB);

    HMODULE mod = nullptr;
    char modName[MAX_PATH] = "unknown";
    uintptr_t modOff = 0;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(rip), &mod) && mod) {
        GetModuleFileNameA(mod, modName, MAX_PATH);
        const char* sl = std::strrchr(modName, '\\');
        if (sl) std::memmove(modName, sl + 1, std::strlen(sl + 1) + 1);
        modOff = rip - reinterpret_cast<uintptr_t>(mod);
    }

    EngineView ev{};
    RAW_EngineView(&ev);

    const std::string path = g_outDir + "\\raw_forensics_crash.json";
    FILE* f = std::fopen(path.c_str(), "w");
    if (f) {
        std::fprintf(f,
            "{\n"
            "  \"exception_code\": \"0x%08lX\",\n"
            "  \"classification\": \"%s\",\n"
            "  \"fault_address\": \"0x%016llX\",\n"
            "  \"rip\": \"0x%016llX\",\n"
            "  \"module\": \"%s\",\n"
            "  \"module_rva\": \"0x%llX\",\n"
            "  \"memory\": {\"load_pct\": %lu, \"avail_phys_mb\": %llu, \"working_set_mb\": %llu},\n"
            "  \"engine_view\": {\"cell\": \"0x%08X\", \"worldspace\": \"0x%08X\", \"interior\": %u, \"game_hour\": %.3f},\n"
            "  \"recent_events\": [\n",
            static_cast<unsigned long>(code), cls,
            static_cast<unsigned long long>(faultAddr), static_cast<unsigned long long>(rip),
            modName, static_cast<unsigned long long>(modOff),
            static_cast<unsigned long>(ms.dwMemoryLoad), static_cast<unsigned long long>(availPhysMB),
            static_cast<unsigned long long>(wsMB),
            ev.cellFormId, ev.worldspaceFormId, ev.interior, ev.gameHour);
        const uint32_t head = g_ringHead.load(std::memory_order_relaxed);
        const uint32_t count = std::min<uint32_t>(head, 32u);
        for (uint32_t i = 0; i < count; ++i) {
            const FlightEvent& e = g_ring[(head - count + i) & (kRingSize - 1u)];
            std::fprintf(f, "    {\"t\":%llu,\"tid\":%u,\"tag\":\"%s\",\"a\":\"0x%llX\",\"b\":\"0x%llX\"}%s\n",
                         static_cast<unsigned long long>(e.tickMs), e.tid, e.tag,
                         static_cast<unsigned long long>(e.a), static_cast<unsigned long long>(e.b),
                         (i + 1 < count) ? "," : "");
        }
        std::fprintf(f, "  ]\n}\n");
        std::fclose(f);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

}  // namespace

void ForensicsInit(const std::string& outDir) {
    g_outDir = outDir;
    g_written.store(false);
    ForensicsRecord("forensics_init");
    if (!g_veh) g_veh = AddVectoredExceptionHandler(0, ForensicsVeh);
}

void ForensicsShutdown() {
    if (g_veh) {
        RemoveVectoredExceptionHandler(g_veh);
        g_veh = nullptr;
    }
}

void ForensicsRecord(const char* tag, uint64_t a, uint64_t b) {
    const uint32_t idx = g_ringHead.fetch_add(1u, std::memory_order_relaxed) & (kRingSize - 1u);
    FlightEvent& e = g_ring[idx];
    e.tickMs = GetTickCount64();
    e.tid = GetCurrentThreadId();
    std::snprintf(e.tag, sizeof e.tag, "%s", tag ? tag : "");
    e.a = a;
    e.b = b;
}

void ForensicsOnPresent() {
    if ((++g_presentCount % 60u) != 1u) return;
    MEMORYSTATUSEX ms{}; ms.dwLength = sizeof ms;
    if (GlobalMemoryStatusEx(&ms)) {
        g_lastMemLoadPct = ms.dwMemoryLoad;
        g_lastAvailPhysMB = ms.ullAvailPhys >> 20;
        if (ms.dwMemoryLoad >= 92u || g_lastAvailPhysMB < 2048ull) {
            ++g_ramPressureEvents;
            ForensicsRecord("ram_pressure", ms.dwMemoryLoad, g_lastAvailPhysMB);
        }
    }
    PROCESS_MEMORY_COUNTERS_EX pmc{}; pmc.cb = sizeof pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof pmc)) {
        g_lastWorkingSetMB = pmc.WorkingSetSize >> 20;
    }
}

std::string ForensicsStatusJson() {
    char b[256];
    std::snprintf(b, sizeof b,
                  "{\"cmd\":\"forensics\",\"mem_load_pct\":%u,\"avail_phys_mb\":%llu,\"working_set_mb\":%llu,"
                  "\"ram_pressure_events\":%u,\"flight_events\":%u}",
                  g_lastMemLoadPct, static_cast<unsigned long long>(g_lastAvailPhysMB),
                  static_cast<unsigned long long>(g_lastWorkingSetMB),
                  g_ramPressureEvents, g_ringHead.load(std::memory_order_relaxed));
    return b;
}

}  // namespace raw::rt
