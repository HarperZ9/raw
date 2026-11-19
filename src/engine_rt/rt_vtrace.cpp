// Vtable slot tracer (RAW engine runtime). Original work, MIT.
//
// For each listed class, every slot of its vtable is pointed at a small thunk we generate:
//     lock inc qword [rip+count]     ; how often the game dispatched through this slot
//     mov rax, [rsp]                 ; the return address: who called
//     mov [rip+lastRet], rax
//     jmp qword [rip+original]       ; on to the game's function, arguments untouched
// rax is volatile and carries no argument in the Windows x64 convention. The vtable entry is one
// aligned 8-byte store, so a thread that reads it sees the old or the new pointer, never half.
// Each present samples the counters; every 300 presents the totals, the frames with calls and up
// to 8 distinct callers per slot go to vt_trace.tsv. Addresses and counts only.
#include "rt_vtrace.h"

#include <windows.h>

#include <cstdio>
#include <cstring>

namespace raw::rt {
namespace {

struct SlotData {
    volatile uint64_t count;
    volatile uint64_t lastRet;
    uint64_t original;
    uint64_t pad;
};

struct Slot {
    std::string cls;
    uint32_t vtable = 0, index = 0, target = 0;
    SlotData* data = nullptr;
    uint64_t lastSeen = 0, frames = 0, prevSnapshot = 0;
    uint32_t callers[8] = {};
    unsigned ncallers = 0;
};

std::vector<Slot> g_slots;
const Image* g_img = nullptr;
std::string g_path;
uint64_t g_presents = 0;

void Emit32(uint8_t*& p, uint32_t v) { std::memcpy(p, &v, 4); p += 4; }
int32_t Rel(const uint8_t* next, const void* target) {
    return int32_t(reinterpret_cast<const uint8_t*>(target) - next);
}

}  // namespace

size_t InstallVTrace(const Image& img, const std::vector<VTraceClass>& classes, const std::string& outPath) {
    g_img = &img;
    g_path = outPath;
    size_t total = 0;
    for (const auto& c : classes) total += c.slots;
    if (total == 0) return 0;
    // The code starts on its own page, so making it executable never takes write access from the counters.
    const size_t dataBytes = (total * sizeof(SlotData) + 4095) & ~size_t(4095), codeBytes = total * 32;
    auto* mem = static_cast<uint8_t*>(VirtualAlloc(nullptr, dataBytes + codeBytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (!mem) return 0;
    auto* data = reinterpret_cast<SlotData*>(mem);
    uint8_t* code = mem + dataBytes;
    size_t k = 0;
    for (const auto& c : classes) {
        auto** vt = reinterpret_cast<uint64_t**>(const_cast<uint8_t*>(img.base) + c.vtable);
        for (uint32_t i = 0; i < c.slots; ++i, ++k) {
            const uint64_t orig = reinterpret_cast<uint64_t>(vt[i]);
            if (!img.InText(orig - uint64_t(img.base))) continue;
            SlotData* sd = &data[k];
            sd->count = 0; sd->lastRet = 0; sd->original = orig;
            uint8_t* t = code + 32 * k;
            uint8_t* p = t;
            *p++ = 0xF0; *p++ = 0x48; *p++ = 0xFF; *p++ = 0x05; Emit32(p, uint32_t(Rel(p + 4, (const void*)&sd->count)));
            *p++ = 0x48; *p++ = 0x8B; *p++ = 0x04; *p++ = 0x24;
            *p++ = 0x48; *p++ = 0x89; *p++ = 0x05; Emit32(p, uint32_t(Rel(p + 4, (const void*)&sd->lastRet)));
            *p++ = 0xFF; *p++ = 0x25; Emit32(p, uint32_t(Rel(p + 4, &sd->original)));
            while (p < t + 32) *p++ = 0xCC;
            Slot s;
            s.cls = c.name; s.vtable = c.vtable; s.index = i; s.data = sd;
            s.target = uint32_t(orig - uint64_t(img.base));
            g_slots.push_back(s);
        }
    }
    DWORD old;
    VirtualProtect(mem, dataBytes, PAGE_READWRITE, &old);
    VirtualProtect(code, codeBytes, PAGE_EXECUTE_READ, &old);
    FlushInstructionCache(GetCurrentProcess(), code, codeBytes);
    // Swap the slots last, once every thunk is in place.
    for (size_t j = 0; j < g_slots.size(); ++j) {
        Slot& s = g_slots[j];
        auto* entry = reinterpret_cast<uint64_t*>(const_cast<uint8_t*>(img.base) + s.vtable + 8 * s.index);
        const size_t thunkIndex = size_t(s.data - data);
        if (VirtualProtect(entry, 8, PAGE_READWRITE, &old)) {
            InterlockedExchange64(reinterpret_cast<volatile LONG64*>(entry), LONG64(code + 32 * thunkIndex));
            VirtualProtect(entry, 8, old, &old);
        }
    }
    return g_slots.size();
}

void VTraceOnPresent() {
    ++g_presents;
    for (Slot& s : g_slots) {
        const uint64_t n = s.data->count;
        if (n != s.lastSeen) {
            ++s.frames;
            s.lastSeen = n;
            const uint64_t ret = s.data->lastRet;
            const uint64_t rva = ret - uint64_t(g_img->base);
            if (g_img->InText(rva)) {
                bool known = false;
                for (unsigned i = 0; i < s.ncallers; ++i) known |= s.callers[i] == uint32_t(rva);
                if (!known && s.ncallers < 8) s.callers[s.ncallers++] = uint32_t(rva);
            }
        }
    }
    if (g_presents % 300 != 0 || g_slots.empty()) return;
    FILE* f = std::fopen(g_path.c_str(), g_presents == 300 ? "w" : "a");
    if (!f) return;
    if (g_presents == 300) std::fputs("presents\tclass\tvtable\tslot\ttarget\tcalls_total\tcalls_window\tframes_with_calls\tcallers\n", f);
    for (Slot& s : g_slots) {
        const uint64_t n = s.data->count;
        std::fprintf(f, "%llu\t%s\t0x%X\t%u\t0x%X\t%llu\t%llu\t%llu\t", (unsigned long long)g_presents, s.cls.c_str(),
                     s.vtable, s.index, s.target, (unsigned long long)n, (unsigned long long)(n - s.prevSnapshot),
                     (unsigned long long)s.frames);
        for (unsigned i = 0; i < s.ncallers; ++i) std::fprintf(f, "%s0x%X", i ? "," : "", s.callers[i]);
        std::fputc('\n', f);
        s.prevSnapshot = n;
    }
    std::fclose(f);
}

}  // namespace raw::rt
