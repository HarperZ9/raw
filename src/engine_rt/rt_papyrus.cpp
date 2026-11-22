// Papyrus: the VM's update timed, its budget fixed or adaptive (RAW engine runtime). Original work, MIT.
#include "rt_papyrus.h"
#include "rt_patch.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <vector>

#include <Zydis/Zydis.h>
#include <intrin.h>

namespace raw::rt {
namespace {

LARGE_INTEGER g_freq{};
// Papyrus timing (written on the game's VM thread, read at present)
using VmFn = void (*)(void*, float);
VmFn g_origUpdate = nullptr, g_origTasklets = nullptr;
std::atomic<int64_t> g_vmTicks{0};
std::atomic<int64_t> g_vmCalls{0};
float g_budgetOverride = -1.0f;
std::atomic<float> g_lastBudget{0.0f};
std::atomic<int64_t> g_vmFrameTicks{0};      // VM time since the last present
// Adaptive budget: the VM gets a share of the time the frame has left before its target (ON by default for smooth 120Hz pacing).
struct Adaptive { bool on = true; float targetMs = 11.0f, minMs = 0.8f, maxMs = 4.5f, share = 0.40f; } g_adapt;
std::atomic<float> g_adaptBudget{-1.0f};
double g_adaptSum = 0; uint64_t g_adaptN = 0;  // the budget given, averaged per measure
double g_costEma = 0;                          // frame time without the VM, smoothed

void Timed(VmFn orig, void* self, float budget) {
    g_lastBudget.store(budget);
    if (const float a = g_adaptBudget.load(); g_adapt.on && a > 0.0f) budget = a;
    else if (g_budgetOverride > 0.0f) budget = g_budgetOverride;
    LARGE_INTEGER a, b;
    QueryPerformanceCounter(&a);
    orig(self, budget);
    QueryPerformanceCounter(&b);
    g_vmTicks += b.QuadPart - a.QuadPart;
    g_vmFrameTicks += b.QuadPart - a.QuadPart;
    ++g_vmCalls;
}

// At a present: frame time minus this frame's VM time is the frame's own cost; what is left before the
// target, times the share, is the next budget, held between min and max.
void AdaptOnPresent(double frameMs) {
    const double vmMs = double(g_vmFrameTicks.exchange(0)) * 1000.0 / double(g_freq.QuadPart);
    if (!g_adapt.on || frameMs <= 0) return;
    const double cost = std::max(0.0, frameMs - vmMs);
    g_costEma = g_costEma <= 0 ? cost : g_costEma * 0.9 + cost * 0.1;
    const double spare = double(g_adapt.targetMs) - g_costEma;
    const float b = float(std::clamp(spare * g_adapt.share, double(g_adapt.minMs), double(g_adapt.maxMs)));
    g_adaptBudget.store(b);
    g_adaptSum += b; ++g_adaptN;
}
void HookUpdate(void* self, float budget) { Timed(g_origUpdate, self, budget); }
void HookTasklets(void* self, float budget) { Timed(g_origTasklets, self, budget); }

// The opcode loop, timed at its call sites: each direct call to it in the VM's update functions is pointed at a
// stub near the image that jumps to LoopHook. The hook forwards six integer arguments and touches no vector
// register, so float arguments and a float return pass through unchanged.
using LoopFn = uint64_t (*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
LoopFn g_loop = nullptr;
std::atomic<uint64_t> g_loopTicks{0}, g_loopCalls{0};
uint64_t g_tscPerMs = 0;

uint64_t LoopHook(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f) {
    const uint64_t t0 = __rdtsc();
    const uint64_t r = g_loop(a, b, c, d, e, f);
    g_loopTicks += __rdtsc() - t0;
    ++g_loopCalls;
    return r;
}

// 14 bytes within 2 GB of the image: jmp [rip+0]; dq target
uint8_t* NearStub(const Image& img, void* target) {
    const uint64_t lo = uint64_t(img.base) + img.sizeOfImage;
    for (uint64_t a = (lo + 0xFFFF) & ~0xFFFFull; a < lo + 0x7FF00000ull; a += 0x10000) {
        auto* p = static_cast<uint8_t*>(VirtualAlloc(reinterpret_cast<void*>(a), 0x1000, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
        if (!p) continue;
        p[0] = 0xFF; p[1] = 0x25; std::memset(p + 2, 0, 4);
        std::memcpy(p + 6, &target, 8);
        FlushInstructionCache(GetCurrentProcess(), p, 14);
        return p;
    }
    return nullptr;
}

}  // namespace

size_t InstallOpcodeLoopTiming(const Image& img, uint32_t vmVtable, uint32_t loopRva, std::string* sites) {
    // calibrate the time-stamp counter against the performance counter once
    LARGE_INTEGER f, q0, q1;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&q0);
    const uint64_t t0 = __rdtsc();
    Sleep(20);
    QueryPerformanceCounter(&q1);
    g_tscPerMs = uint64_t(double(__rdtsc() - t0) / (double(q1.QuadPart - q0.QuadPart) * 1000.0 / double(f.QuadPart)));
    // Every direct call to the loop in the code section: a byte scan for E8 with a matching displacement, each
    // candidate confirmed by decoding its function from the start and landing on the site as an instruction.
    // (The callers sit under CodeTasklet, which may run on job threads; the hook keeps only atomic counters.)
    (void)vmVtable;
    ZydisDecoder dec;
    ZydisDecoderInit(&dec, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64);
    std::vector<uint64_t> callSites;
    for (uint64_t p = img.textBegin; p + 5 <= img.textEnd; ++p) {
        if (img.base[p] != 0xE8) continue;
        int32_t rel;
        std::memcpy(&rel, img.base + p + 1, 4);
        if (int64_t(p) + 5 + rel != int64_t(loopRva)) continue;
        const long fi = img.FuncIndex(p);
        if (fi < 0) continue;
        const uint64_t end = img.funcs[size_t(fi)].second;
        ZydisDecodedInstruction in;
        uint64_t q = img.funcs[size_t(fi)].first;
        while (q < p && ZYAN_SUCCESS(ZydisDecoderDecodeInstruction(&dec, nullptr, img.base + q, size_t(end - q), &in))) q += in.length;
        if (q == p) callSites.push_back(p);
    }
    if (callSites.empty()) return 0;
    g_loop = reinterpret_cast<LoopFn>(const_cast<uint8_t*>(img.base) + loopRva);
    uint8_t* stub = NearStub(img, reinterpret_cast<void*>(&LoopHook));
    if (!stub) return 0;
    for (uint64_t site : callSites) {
        auto* rel = const_cast<uint8_t*>(img.base) + site + 1;
        const int32_t v = int32_t(int64_t(stub) - int64_t(img.base + site + 5));
        if (!PatchWrite(rel, &v, 4)) continue;
        if (sites) { char b[24]; std::snprintf(b, sizeof b, "%s0x%llX", sites->empty() ? "" : ",", (unsigned long long)site); *sites += b; }
    }
    return callSites.size();
}

bool InstallPapyrusTiming(const Image& img, uint32_t vmVtable, float budgetOverrideMs) {
    g_budgetOverride = budgetOverrideMs;
    auto* vt = reinterpret_cast<uint64_t*>(const_cast<uint8_t*>(img.base) + vmVtable);
    g_origUpdate = reinterpret_cast<VmFn>(vt[4]);
    g_origTasklets = reinterpret_cast<VmFn>(vt[5]);
    return PatchPointer(vt + 4, reinterpret_cast<void*>(&HookUpdate)) && PatchPointer(vt + 5, reinterpret_cast<void*>(&HookTasklets));
}


void PapyrusOnPresent(double frameMs) {
    if (!g_freq.QuadPart) QueryPerformanceFrequency(&g_freq);
    AdaptOnPresent(frameMs);
}

void PapyrusReset() { g_vmTicks = 0; g_vmCalls = 0; g_adaptSum = 0; g_adaptN = 0; g_loopTicks = 0; g_loopCalls = 0; }

std::string PapyrusStats() {
    if (!g_freq.QuadPart) QueryPerformanceFrequency(&g_freq);
    const double vmMs = double(g_vmTicks.exchange(0)) * 1000.0 / double(g_freq.QuadPart);
    const int64_t vmCalls = g_vmCalls.exchange(0);
    char b[256];
    std::snprintf(b, sizeof b, ",\"vm_ms_total\":%.3f,\"vm_calls\":%lld,\"vm_budget_in\":%.3f", vmMs, (long long)vmCalls,
                  double(g_lastBudget.load()));
    std::string out = b;
    if (g_loop && g_tscPerMs) {
        std::snprintf(b, sizeof b, ",\"opcode_loop_ms_total\":%.3f,\"opcode_loop_calls\":%llu",
                      double(g_loopTicks.exchange(0)) / double(g_tscPerMs), (unsigned long long)g_loopCalls.exchange(0));
        out += b;
    }
    if (g_adapt.on) {
        std::snprintf(b, sizeof b, ",\"vm_budget_adaptive\":%.3f,\"frame_cost_ema\":%.3f", g_adaptN ? g_adaptSum / double(g_adaptN) : 0.0,
                      g_costEma);
        out += b;
    }
    g_adaptSum = 0; g_adaptN = 0;
    return out;
}

std::string PapyrusCommand(const std::string& a, const std::string& rest) {
    // papyrus adaptive <target ms> <min ms> <max ms> | papyrus fixed <ms> | papyrus off
    std::istringstream in(rest);
    if (a == "adaptive") {
        in >> g_adapt.targetMs >> g_adapt.minMs >> g_adapt.maxMs;
        g_adapt.on = true; g_costEma = 0; g_adaptBudget.store(-1.0f);
    } else if (a == "fixed") {
        g_adapt.on = false; in >> g_budgetOverride;
    } else {
        g_adapt.on = false; g_budgetOverride = -1.0f;
    }
    char b[224];
    std::snprintf(b, sizeof b, "{\"cmd\":\"papyrus\",\"mode\":\"%s\",\"target\":%.3f,\"min\":%.3f,\"max\":%.3f,\"fixed\":%.3f}",
                  a.c_str(), g_adapt.targetMs, g_adapt.minMs, g_adapt.maxMs, g_budgetOverride);
    return b;
}

}  // namespace raw::rt
