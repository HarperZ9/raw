// In-process sampling profiler (RAW engine runtime). Original work, MIT.
//
// A background thread suspends each game thread in turn, copies its instruction pointer and the top of its
// stack into a preallocated buffer, and resumes it at once; nothing is allocated while a thread is suspended.
// Afterwards each sample is attributed to its .pdata function, and the copied stack is scanned for return
// addresses (a word that points into the code section just past a call instruction). The scan is a
// heuristic: it finds callers that left their return address on the stack, and can include stale words.
#include "rt_sampler.h"

#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <timeapi.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <map>
#include <thread>
#include <unordered_map>
#include <vector>

#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "psapi.lib")

namespace raw::rt {
namespace {

constexpr size_t kStackBytes = 2048;
constexpr size_t kStackSlots = 16000;  // 32 MB of stack copies at most
std::atomic<bool> g_running{false};
std::atomic<DWORD> g_renderThread{0};

// a return address: the bytes before it are a call (E8 rel32, FF /2 with the common encodings)
bool IsReturnAddress(const Image& img, uint64_t rva) {
    if (!img.InText(rva) || rva < img.textBegin + 7) return false;
    const uint8_t* p = img.base + rva;
    if (p[-5] == 0xE8) return true;
    if (p[-2] == 0xFF && (p[-1] & 0x38) == 0x10) return true;                     // call reg / [reg]
    if (p[-3] == 0xFF && (p[-2] & 0x38) == 0x10) return true;                     // call [reg+disp8]
    if (p[-6] == 0xFF && (p[-5] & 0x38) == 0x10) return true;                     // call [rip+disp32] / [reg+disp32]
    if (p[-7] == 0xFF && (p[-6] & 0x38) == 0x10) return true;                     // with SIB
    return false;
}

bool CopyStack(void* dst, const void* src, size_t n) {
    __try { std::memcpy(dst, src, n); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// module ranges, for "where does this thread's time go": the game, our proxy, the driver, the system
struct Module { uint64_t lo, hi; std::string name; };
std::vector<Module> Modules() {
    std::vector<Module> out;
    HMODULE mods[1024];
    DWORD need = 0;
    if (!EnumProcessModules(GetCurrentProcess(), mods, sizeof mods, &need)) return out;
    for (DWORD i = 0; i < std::min<DWORD>(need / sizeof(HMODULE), 1024); ++i) {
        MODULEINFO mi{};
        char name[MAX_PATH] = "";
        if (!GetModuleInformation(GetCurrentProcess(), mods[i], &mi, sizeof mi)) continue;
        GetModuleFileNameA(mods[i], name, MAX_PATH);
        // the proxy and the system runtime share the name d3d11.dll: keep the folder to tell them apart
        std::string full(name);
        const size_t slash = full.rfind(char(92));
        const size_t slash2 = slash == std::string::npos ? std::string::npos : full.rfind(char(92), slash - 1);
        std::string label = slash2 == std::string::npos ? full : full.substr(slash2 + 1);
        for (auto& ch : label) if (ch == char(92)) ch = '/';
        out.push_back({uint64_t(mi.lpBaseOfDll), uint64_t(mi.lpBaseOfDll) + mi.SizeOfImage, label});
    }
    return out;
}

struct Sample { DWORD tid; uint64_t rip; uint32_t stackLen; };

std::string Top(const std::unordered_map<uint32_t, uint64_t>& m, size_t n, const char* key) {
    std::vector<std::pair<uint32_t, uint64_t>> v(m.begin(), m.end());
    std::sort(v.begin(), v.end(), [](auto& a, auto& b) { return a.second > b.second; });
    std::string out = std::string(",\"") + key + "\":[";
    char b[48];
    for (size_t i = 0; i < std::min(n, v.size()); ++i) {
        std::snprintf(b, sizeof b, "%s[\"0x%X\",%llu]", i ? "," : "", v[i].first, (unsigned long long)v[i].second);
        out += b;
    }
    return out + "]";
}

void Run(const Image* img, double seconds, uint32_t lo, uint32_t hi, void (*result)(const std::string&)) {
    const DWORD self = GetCurrentThreadId(), pid = GetCurrentProcessId();
    std::vector<std::pair<DWORD, HANDLE>> threads;
    if (HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0); snap != INVALID_HANDLE_VALUE) {
        THREADENTRY32 te{sizeof te};
        for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
            if (te.th32OwnerProcessID != pid || te.th32ThreadID == self) continue;
            if (HANDLE h = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, te.th32ThreadID))
                threads.push_back({te.th32ThreadID, h});
        }
        CloseHandle(snap);
    }
    const size_t cap = 400000;
    std::vector<Sample> samples;
    samples.reserve(cap);
    std::vector<uint8_t> stacks(kStackSlots * kStackBytes);  // stacks kept only for samples inside [lo, hi)
    size_t stackUsed = 0;
    std::vector<uint32_t> stackOf;                       // sample index -> stack slot, or UINT32_MAX
    stackOf.reserve(cap);
    timeBeginPeriod(1);
    LARGE_INTEGER f, t0, t;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&t0);
    uint64_t ticks = 0;
    do {
        for (auto& [tid, h] : threads) {
            if (samples.size() >= cap) break;
            CONTEXT c{};
            c.ContextFlags = CONTEXT_CONTROL;
            if (SuspendThread(h) == DWORD(-1)) continue;
            uint32_t slot = UINT32_MAX;
            if (GetThreadContext(h, &c)) {
                const uint64_t rva = c.Rip - uint64_t(img->base);
                if (rva >= lo && rva < hi && stackUsed + kStackBytes <= stacks.size()) {
                    // copy the stack top under SEH: the page past the stack's end may be unmapped
                    if (CopyStack(stacks.data() + stackUsed, reinterpret_cast<void*>(c.Rsp), kStackBytes)) {
                        slot = uint32_t(stackUsed / kStackBytes);
                        stackUsed += kStackBytes;
                    }
                }
            }
            ResumeThread(h);
            if (c.Rip) { samples.push_back({tid, c.Rip, 0}); stackOf.push_back(slot); }
        }
        ++ticks;
        Sleep(1);
        QueryPerformanceCounter(&t);
    } while (double(t.QuadPart - t0.QuadPart) / double(f.QuadPart) < seconds && samples.size() < cap);
    timeEndPeriod(1);
    for (auto& [tid, h] : threads) CloseHandle(h);

    std::unordered_map<uint32_t, uint64_t> all, inRange, callers, rangeThreads, firstOutside;
    uint64_t inGame = 0, renderInRange = 0;
    for (size_t i = 0; i < samples.size(); ++i) {
        const uint64_t rva = samples[i].rip - uint64_t(img->base);
        if (!img->InText(rva)) continue;
        ++inGame;
        const uint32_t fn = img->FuncStart(rva);
        ++all[fn];
        if (rva < lo || rva >= hi) continue;
        ++inRange[fn];
        ++rangeThreads[samples[i].tid];
        if (samples[i].tid == g_renderThread.load()) ++renderInRange;
        if (stackOf[i] == UINT32_MAX) continue;
        const auto* w = reinterpret_cast<const uint64_t*>(stacks.data() + size_t(stackOf[i]) * kStackBytes);
        std::vector<uint32_t> seen;
        bool outsideFound = false;
        for (size_t k = 0; k < kStackBytes / 8 && seen.size() < 24; ++k) {
            const uint64_t r = w[k] - uint64_t(img->base);
            if (w[k] < uint64_t(img->base) || !IsReturnAddress(*img, r)) continue;
            const uint32_t cf = img->FuncStart(r);
            if (!cf || std::find(seen.begin(), seen.end(), cf) != seen.end()) continue;
            seen.push_back(cf);
            ++callers[cf];
            if (!outsideFound && (cf < lo || cf >= hi)) { ++firstOutside[cf]; outsideFound = true; }
        }
    }
    // the busiest thread outside the job system's wait: samples per module
    std::unordered_map<DWORD, uint64_t> perThread;
    for (const auto& sm : samples) {
        const uint64_t rva = sm.rip - uint64_t(img->base);
        if (img->InText(rva)) ++perThread[sm.tid];
    }
    DWORD busiest = 0; uint64_t most = 0;
    for (auto& [tid, n] : perThread) if (n > most) { most = n; busiest = tid; }
    const auto mods = Modules();
    std::map<std::string, uint64_t> byModule;
    uint64_t busiestAll = 0;
    for (const auto& sm : samples) {
        if (sm.tid != busiest) continue;
        ++busiestAll;
        std::string m = "other";
        for (const auto& md : mods) if (sm.rip >= md.lo && sm.rip < md.hi) { m = md.name; break; }
        ++byModule[m];
    }
    char head[256];
    std::snprintf(head, sizeof head,
                  "{\"cmd\":\"sample\",\"seconds\":%.1f,\"threads\":%zu,\"ticks\":%llu,\"samples\":%zu,\"in_game\":%llu,"
                  "\"range\":[\"0x%X\",\"0x%X\"],\"in_range_render_thread\":%llu",
                  seconds, threads.size(), (unsigned long long)ticks, samples.size(), (unsigned long long)inGame, lo, hi,
                  (unsigned long long)renderInRange);
    std::string out = head;
    out += Top(all, 30, "top_all");
    out += Top(inRange, 40, "top_range");
    out += Top(callers, 40, "range_callers");
    out += Top(firstOutside, 20, "range_entry_from");
    out += Top(rangeThreads, 16, "range_threads");
    out += ",\"busiest_thread\":" + std::to_string(busiest) + ",\"busiest_samples\":" + std::to_string(busiestAll) + ",\"busiest_by_module\":{";
    {
        std::vector<std::pair<std::string, uint64_t>> v(byModule.begin(), byModule.end());
        std::sort(v.begin(), v.end(), [](auto& a, auto& b) { return a.second > b.second; });
        for (size_t i = 0; i < std::min<size_t>(v.size(), 12); ++i)
            out += (i ? ",\"" : "\"") + v[i].first + "\":" + std::to_string(v[i].second);
    }
    out += "}";
    // where inside the proxy (the d3d11.dll in the game folder): the busiest thread's top offsets
    std::unordered_map<uint32_t, uint64_t> proxyOffsets;
    for (const auto& md : mods) {
        if (md.name.find("Skyrim") == std::string::npos || md.name.find("d3d11.dll") == std::string::npos) continue;
        for (const auto& sm : samples)
            if (sm.tid == busiest && sm.rip >= md.lo && sm.rip < md.hi) ++proxyOffsets[uint32_t((sm.rip - md.lo) & ~0xFull)];
    }
    out += Top(proxyOffsets, 40, "proxy_offsets");
    // the busiest thread's hottest game functions
    std::unordered_map<uint32_t, uint64_t> busiestGame;
    for (const auto& sm : samples) {
        const uint64_t rva = sm.rip - uint64_t(img->base);
        if (sm.tid == busiest && img->InText(rva)) ++busiestGame[img->FuncStart(rva)];
    }
    out += Top(busiestGame, 30, "busiest_game_functions");
    char rt[48];
    std::snprintf(rt, sizeof rt, ",\"render_thread\":%lu}", g_renderThread.load());
    result(out + rt);
    g_running = false;
}

}  // namespace

void SamplerNoteRenderThread() { g_renderThread = GetCurrentThreadId(); }

bool StartSampler(const Image& img, double seconds, uint32_t lo, uint32_t hi, void (*result)(const std::string&)) {
    if (g_running.exchange(true)) return false;
    std::thread([&img, seconds, lo, hi, result] { Run(&img, seconds, lo, hi, result); }).detach();
    return true;
}

}  // namespace raw::rt
