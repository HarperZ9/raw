// CPU time of engine functions: vtable slots and function pointers loaded by a RIP-relative lea, timed live
// (RAW engine runtime). Original work, MIT.
//
// Each timed function gets its own hook (a template instance). A hook forwards six integer arguments and
// touches no vector register, so float arguments and returns pass through. Time is counted for the outermost
// call on each thread only, so recursive functions (scene-graph culling) are not counted twice; every call
// is counted. Times are read with the time-stamp counter, calibrated against the performance counter.
#include "rt_vtime.h"

#include <windows.h>
#include <intrin.h>

#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <utility>

#include <Zydis/Zydis.h>

namespace raw::rt {
namespace {

using Fn = uint64_t (*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
constexpr int kSlots = 16;

struct Slot {
    Fn orig = nullptr;
    std::string label;
    std::atomic<uint64_t> ticks{0}, calls{0};
};
Slot g_slots[kSlots];
int g_used = 0;
thread_local int t_depth[kSlots];
uint64_t g_tscPerMs = 0;
uint64_t g_frames = 0;

template <int N>
uint64_t Hook(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f) {
    Slot& s = g_slots[N];
    ++s.calls;
    if (t_depth[N]++ != 0) {
        const uint64_t r = s.orig(a, b, c, d, e, f);
        --t_depth[N];
        return r;
    }
    const uint64_t t0 = __rdtsc();
    const uint64_t r = s.orig(a, b, c, d, e, f);
    s.ticks += __rdtsc() - t0;
    --t_depth[N];
    return r;
}

template <int... I>
constexpr std::array<void*, sizeof...(I)> HookTable(std::integer_sequence<int, I...>) {
    return {reinterpret_cast<void*>(&Hook<I>)...};
}
const auto kHooks = HookTable(std::make_integer_sequence<int, kSlots>{});

void Calibrate() {
    if (g_tscPerMs) return;
    LARGE_INTEGER f, q0, q1;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&q0);
    const uint64_t t0 = __rdtsc();
    Sleep(20);
    QueryPerformanceCounter(&q1);
    g_tscPerMs = uint64_t(double(__rdtsc() - t0) / (double(q1.QuadPart - q0.QuadPart) * 1000.0 / double(f.QuadPart)));
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

std::string VTimeSlot(const Image& img, uint32_t vtable, uint32_t slot, const std::string& label) {
    if (g_used >= kSlots) return "vtime: no free hook";
    Calibrate();
    auto* entry = reinterpret_cast<uint64_t*>(const_cast<uint8_t*>(img.base) + vtable) + slot;
    const uint64_t fn = *entry - uint64_t(img.base);
    if (!img.InText(fn)) return "vtime: slot does not point into the code section";
    Slot& s = g_slots[g_used];
    s.orig = reinterpret_cast<Fn>(*entry);
    s.label = label;
    DWORD old;
    if (!VirtualProtect(entry, 8, PAGE_READWRITE, &old)) return "vtime: VirtualProtect failed";
    InterlockedExchange64(reinterpret_cast<volatile LONG64*>(entry), LONG64(kHooks[size_t(g_used)]));
    VirtualProtect(entry, 8, old, &old);
    ++g_used;
    char b[160];
    std::snprintf(b, sizeof b, "vtime: %s = vtable 0x%X slot 0x%X -> 0x%llX", label.c_str(), vtable, slot, (unsigned long long)fn);
    return b;
}

std::string VTimeLea(const Image& img, uint32_t site, const std::string& label) {
    if (g_used >= kSlots) return "vtime: no free hook";
    Calibrate();
    ZydisDecoder dec;
    ZydisDecoderInit(&dec, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64);
    ZydisDecodedInstruction in;
    ZydisDecodedOperand op[ZYDIS_MAX_OPERAND_COUNT];
    if (!ZYAN_SUCCESS(ZydisDecoderDecodeFull(&dec, img.base + site, 15, &in, op)) || in.mnemonic != ZYDIS_MNEMONIC_LEA ||
        op[1].type != ZYDIS_OPERAND_TYPE_MEMORY || op[1].mem.base != ZYDIS_REGISTER_RIP || in.raw.disp.size != 32)
        return "vtime: not a RIP-relative lea";
    ZyanU64 target = 0;
    ZydisCalcAbsoluteAddress(&in, &op[1], site, &target);
    if (!img.InText(target)) return "vtime: lea target is not code";
    Slot& s = g_slots[g_used];
    s.orig = reinterpret_cast<Fn>(const_cast<uint8_t*>(img.base) + target);
    s.label = label;
    uint8_t* stub = NearStub(img, kHooks[size_t(g_used)]);
    if (!stub) return "vtime: no stub near the image";
    auto* disp = const_cast<uint8_t*>(img.base) + site + in.raw.disp.offset;
    const int32_t v = int32_t(int64_t(stub) - int64_t(img.base + site + in.length));
    DWORD old;
    if (!VirtualProtect(disp, 4, PAGE_EXECUTE_READWRITE, &old)) return "vtime: VirtualProtect failed";
    std::memcpy(disp, &v, 4);
    VirtualProtect(disp, 4, old, &old);
    FlushInstructionCache(GetCurrentProcess(), disp, 4);
    ++g_used;
    char b[160];
    std::snprintf(b, sizeof b, "vtime: %s = lea at 0x%X -> 0x%llX", label.c_str(), site, (unsigned long long)target);
    return b;
}

void VTimeOnPresent() { ++g_frames; }

void VTimeReset() {
    for (int i = 0; i < g_used; ++i) { g_slots[i].ticks = 0; g_slots[i].calls = 0; }
    g_frames = 0;
}

std::string VTimeStats() {
    if (!g_used || !g_tscPerMs) return "";
    const double fr = g_frames ? double(g_frames) : 1.0;
    std::string ms = ",\"cpu_ms\":{", calls = ",\"cpu_calls\":{";
    char b[96];
    for (int i = 0; i < g_used; ++i) {
        std::snprintf(b, sizeof b, "%s\"%s\":%.4f", i ? "," : "", g_slots[i].label.c_str(),
                      double(g_slots[i].ticks.exchange(0)) / double(g_tscPerMs) / fr);
        ms += b;
        std::snprintf(b, sizeof b, "%s\"%s\":%.1f", i ? "," : "", g_slots[i].label.c_str(), double(g_slots[i].calls.exchange(0)) / fr);
        calls += b;
    }
    g_frames = 0;
    return ms + "}" + calls + "}";
}

}  // namespace raw::rt
