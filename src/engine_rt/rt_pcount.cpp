// Papyrus throughput (RAW engine runtime). Original work, MIT.
//
// tools/papyrus/make_load_plugin.py gives each test quest a global, RAWLoadCountNN, and every OnUpdate writes its own
// running count there (one native call), just before it registers the next update. The sum over the quests, read at
// each measure, is the number of updates the VM completed. Even quests run the arithmetic loop, odd quests the loop with
// a native call per iteration, so the two are reported apart.
//
// The globals are found through the data handler's form array for globals (TESDataHandler, formArrays at +0x10, one
// BSTArray of 0x18 bytes per form type, globals type 9) and named by their editor ID (TESGlobal: BSString at +0x20,
// value at +0x34). Offsets from the public CommonLibSSE-NG headers for 1.6; each read is under structured exception
// handling, and a layout that does not hold finds nothing rather than faulting.
#include "rt_pcount.h"

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace raw::rt {
namespace {

const Image* g_img = nullptr;
const std::vector<RefTarget>* g_refs = nullptr;
struct Counter { const float* value; int quest; };
std::vector<Counter> g_counters;
double g_lastSum[2] = {0, 0};
LARGE_INTEGER g_lastAt{}, g_freq{};
int g_scans = 0;

bool Read(const void* p, void* out, size_t n) {
    __try { std::memcpy(out, p, n); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

uint64_t DataHandler() {
    for (const auto& r : *g_refs)
        if (r.name == "TESDataHandler::Singleton") {
            uint64_t p = 0;
            return Read(g_img->base + r.rva, &p, 8) ? p : 0;
        }
    return 0;
}

// The plugin's globals, found once they are loaded (the data handler fills its arrays as plugins load).
void Scan() {
    if (!g_counters.empty() || ++g_scans > 1000) return;
    const uint64_t dh = DataHandler();
    if (!dh) return;
    uint64_t data = 0; uint32_t size = 0;
    if (!Read(reinterpret_cast<void*>(dh + 0x10 + 9 * 0x18), &data, 8) ||
        !Read(reinterpret_cast<void*>(dh + 0x10 + 9 * 0x18 + 0x10), &size, 4) || !data || size > 1000000) return;
    for (uint32_t i = 0; i < size; ++i) {
        uint64_t form = 0, eid = 0;
        if (!Read(reinterpret_cast<void*>(data + 8ull * i), &form, 8) || !form) continue;
        if (!Read(reinterpret_cast<void*>(form + 0x20), &eid, 8) || !eid) continue;
        char name[32] = {};
        if (!Read(reinterpret_cast<void*>(eid), name, sizeof name - 1)) continue;
        if (std::strncmp(name, "RAWLoadCount", 12) != 0) continue;
        g_counters.push_back({reinterpret_cast<const float*>(form + 0x34), std::atoi(name + 12)});
    }
}

void Sums(double out[2]) {
    out[0] = out[1] = 0;
    for (const auto& c : g_counters) {
        float v = 0;
        if (Read(c.value, &v, 4)) out[c.quest & 1] += v;
    }
}

}  // namespace

void PapyrusCountInit(const Image& img, const std::vector<RefTarget>* refs) {
    g_img = &img; g_refs = refs;
    QueryPerformanceFrequency(&g_freq);
}

std::string PapyrusCountStats() {
    if (!g_img) return "";
    Scan();
    if (g_counters.empty()) return "";
    double now[2];
    Sums(now);
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    const double secs = g_lastAt.QuadPart ? double(t.QuadPart - g_lastAt.QuadPart) / double(g_freq.QuadPart) : 0.0;
    char b[256];
    std::snprintf(b, sizeof b,
                  ",\"papyrus_updates\":{\"quests\":%zu,\"seconds\":%.3f,\"arith\":%.0f,\"native\":%.0f,"
                  "\"arith_per_s\":%.2f,\"native_per_s\":%.2f}",
                  g_counters.size(), secs, now[0] - g_lastSum[0], now[1] - g_lastSum[1],
                  secs > 0 ? (now[0] - g_lastSum[0]) / secs : 0.0, secs > 0 ? (now[1] - g_lastSum[1]) / secs : 0.0);
    g_lastSum[0] = now[0]; g_lastSum[1] = now[1]; g_lastAt = t;
    return secs > 0 ? b : "";
}

std::string PapyrusCountJson() {
    if (!g_img) return "{\"cmd\":\"papyrus\",\"error\":\"not initialised\"}";
    Scan();
    double now[2];
    Sums(now);
    char b[192];
    std::snprintf(b, sizeof b, "{\"cmd\":\"papyrus\",\"op\":\"count\",\"globals\":%zu,\"scans\":%d,\"arith\":%.0f,\"native\":%.0f}",
                  g_counters.size(), g_scans, now[0], now[1]);
    return b;
}

}  // namespace raw::rt
