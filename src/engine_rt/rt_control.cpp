// Engine control: settings, the go-file protocol, frame time and Papyrus timing. Original work, MIT.
#include "rt_control.h"

#include <windows.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <fstream>
#include <sstream>
#include <string>

#include "rt_atlas.h"
#include "rt_capture.h"

namespace raw::rt {
namespace {

const Image* g_img = nullptr;
const std::vector<RefTarget>* g_refs = nullptr;
std::string g_game, g_out;
LARGE_INTEGER g_freq{}, g_last{};
std::deque<double> g_frameMs;          // recent present-to-present intervals
std::deque<std::string> g_cmds;        // pending go-file commands
int g_wait = 0;
bool g_busy = false;
void (*g_invalidate)() = nullptr;

// Papyrus timing (written on the game's VM thread, read at present)
using VmFn = void (*)(void*, float);
VmFn g_origUpdate = nullptr, g_origTasklets = nullptr;
std::atomic<int64_t> g_vmTicks{0};
std::atomic<int64_t> g_vmCalls{0};
float g_budgetOverride = -1.0f;
std::atomic<float> g_lastBudget{0.0f};

void Timed(VmFn orig, void* self, float budget) {
    g_lastBudget.store(budget);
    if (g_budgetOverride > 0.0f) budget = g_budgetOverride;
    LARGE_INTEGER a, b;
    QueryPerformanceCounter(&a);
    orig(self, budget);
    QueryPerformanceCounter(&b);
    g_vmTicks += b.QuadPart - a.QuadPart;
    ++g_vmCalls;
}
void HookUpdate(void* self, float budget) { Timed(g_origUpdate, self, budget); }
void HookTasklets(void* self, float budget) { Timed(g_origTasklets, self, budget); }

const RefTarget* Find(const std::string& name) {
    for (const auto& r : *g_refs)
        if (r.name == name) return &r;
    return nullptr;
}

void Result(const std::string& json) {
    FILE* f = std::fopen((g_out + "\\control_results.jsonl").c_str(), "a");
    if (f) { std::fprintf(f, "%s\n", json.c_str()); std::fclose(f); }
}

std::string Stats(const char* name, size_t frames) {
    std::vector<double> v(g_frameMs.end() - std::min(frames, g_frameMs.size()), g_frameMs.end());
    double sum = 0, mx = 0;
    for (double x : v) { sum += x; mx = std::max(mx, x); }
    std::sort(v.begin(), v.end());
    const double med = v.empty() ? 0 : v[v.size() / 2], p99 = v.empty() ? 0 : v[size_t(v.size() * 0.99)];
    const double vmMs = double(g_vmTicks.exchange(0)) * 1000.0 / double(g_freq.QuadPart);
    const int64_t vmCalls = g_vmCalls.exchange(0);
    char buf[512];
    std::snprintf(buf, sizeof buf,
                  "{\"cmd\":\"measure\",\"name\":\"%s\",\"frames\":%zu,\"mean_ms\":%.3f,\"median_ms\":%.3f,\"p99_ms\":%.3f,"
                  "\"max_ms\":%.3f,\"vm_ms_total\":%.3f,\"vm_calls\":%lld,\"vm_budget_in\":%.3f}",
                  name, v.size(), v.empty() ? 0 : sum / double(v.size()), med, p99, mx, vmMs, (long long)vmCalls,
                  double(g_lastBudget.load()));
    return buf;
}

void PollGoFile() {
    const std::string go = g_game + "raw_engine_go.txt";
    std::ifstream in(go);
    if (!in) return;
    std::string line;
    while (std::getline(in, line))
        if (!line.empty()) g_cmds.push_back(line);
    in.close();
    DeleteFileA(go.c_str());
    g_busy = !g_cmds.empty();
}

void Step(ID3D11DeviceContext* ctx, IDXGISwapChain* sc) {
    if (g_wait > 0) { --g_wait; return; }
    while (!g_cmds.empty()) {
        std::istringstream ss(g_cmds.front());
        g_cmds.pop_front();
        std::string op, a, b;
        ss >> op >> a;
        std::getline(ss, b);
        while (!b.empty() && b.front() == ' ') b.erase(b.begin());
        if (op == "set") {
            std::string before;
            const bool ok = SetSetting(*g_img, *g_refs, a, b, &before);
            Result("{\"cmd\":\"set\",\"name\":\"" + a + "\",\"value\":\"" + b + "\",\"before\":\"" + before +
                   "\",\"ok\":" + (ok ? "true" : "false") + "}");
        } else if (op == "wait") {
            g_wait = std::atoi(a.c_str());
            return;
        } else if (op == "measure") {
            g_vmTicks = 0; g_vmCalls = 0;
            g_cmds.push_front("report " + a + " " + b);
            g_wait = std::atoi(b.c_str());
            return;
        } else if (op == "report") {
            Result(Stats(a.c_str(), size_t(std::atoi(b.c_str()))));
        } else if (op == "atlas") {
            // atlas <entry> <size>: rebuild a shadow depth target at a new size, live
            const std::string line = ResizeShadowAtlasLive(ctx, *g_img, *g_refs, std::atoi(a.c_str()),
                                                           uint32_t(std::strtoul(b.c_str(), nullptr, 10)));
            if (g_invalidate) g_invalidate();
            Result("{\"cmd\":\"atlas\",\"result\":\"" + line + "\"}");
        } else if (op == "probe") {
            Result(ProbeFunction(*g_img, uint32_t(std::strtoul(a.c_str(), nullptr, 16))));
        } else if (op == "dump") {
            Result(DumpDepthTargets(*g_img, *g_refs));
        } else if (op == "capture") {
            const bool ok = CaptureBackbuffer(ctx, sc, g_out + "\\" + a + ".bmp");
            Result("{\"cmd\":\"capture\",\"name\":\"" + a + "\",\"ok\":" + (ok ? "true" : "false") + "}");
        }
    }
    g_busy = false;
    FILE* f = std::fopen((g_game + "raw_engine_done.txt").c_str(), "w");
    if (f) { std::fputs("done\n", f); std::fclose(f); }
}

}  // namespace

bool SetSetting(const Image& img, const std::vector<RefTarget>& refs, const std::string& name, const std::string& value,
                std::string* before) {
    const RefTarget* r = nullptr;
    for (const auto& x : refs)
        if (x.setting && x.name == name) { r = &x; break; }
    if (!r || name.empty()) return false;
    auto* field = const_cast<uint8_t*>(img.base) + r->rva + 8;
    char old[64] = "";
    const char t = name[0];
    if (t == 'f') {
        float o; std::memcpy(&o, field, 4); std::snprintf(old, sizeof old, "%g", o);
        const float v = std::strtof(value.c_str(), nullptr); std::memcpy(field, &v, 4);
    } else if (t == 'i') {
        int32_t o; std::memcpy(&o, field, 4); std::snprintf(old, sizeof old, "%d", o);
        const int32_t v = std::atoi(value.c_str()); std::memcpy(field, &v, 4);
    } else if (t == 'u') {
        uint32_t o; std::memcpy(&o, field, 4); std::snprintf(old, sizeof old, "%u", o);
        const uint32_t v = uint32_t(std::strtoul(value.c_str(), nullptr, 10)); std::memcpy(field, &v, 4);
    } else if (t == 'b') {
        std::snprintf(old, sizeof old, "%d", int(field[0]));
        field[0] = (value == "1" || value == "true") ? 1 : 0;
    } else {
        return false;  // strings and colours are not written here
    }
    if (before) *before = old;
    return true;
}

size_t ApplyStartupSettings(const Image& img, const std::vector<RefTarget>& refs, const std::string& ini,
                            const std::string& logPath) {
    char buf[16384] = {};
    const DWORD n = GetPrivateProfileSectionA("EngineSet", buf, sizeof buf, ini.c_str());
    size_t applied = 0;
    FILE* log = std::fopen(logPath.c_str(), "a");
    for (const char* p = buf; p < buf + n && *p; p += std::strlen(p) + 1) {
        std::string kv(p);
        const size_t eq = kv.rfind('=');
        if (eq == std::string::npos) continue;
        std::string before;
        const bool ok = SetSetting(img, refs, kv.substr(0, eq), kv.substr(eq + 1), &before);
        applied += ok;
        if (log) std::fprintf(log, "startup set %s (was %s): %s\n", kv.c_str(), before.c_str(), ok ? "ok" : "not found");
    }
    if (log) std::fclose(log);
    return applied;
}

void ControlInit(const Image& img, const std::vector<RefTarget>* refs, const std::string& gameDir, const std::string& outDir,
                 void (*invalidateStateCache)()) {
    g_img = &img; g_refs = refs; g_game = gameDir; g_out = outDir; g_invalidate = invalidateStateCache;
    QueryPerformanceFrequency(&g_freq);
}

void ControlOnPresent(ID3D11DeviceContext* ctx, IDXGISwapChain* sc) {
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    if (g_last.QuadPart) {
        g_frameMs.push_back(double(now.QuadPart - g_last.QuadPart) * 1000.0 / double(g_freq.QuadPart));
        if (g_frameMs.size() > 4096) g_frameMs.pop_front();
    }
    g_last = now;
    AtlasOnPresent();
    if (!g_busy) {
        static unsigned tick = 0;
        if (++tick % 15 == 0) PollGoFile();
    }
    if (g_busy) Step(ctx, sc);
}

std::string ResizeShadowAtlas(const Image& img, const std::vector<RefTarget>& refs, uint32_t size) {
    const RefTarget* r = nullptr;
    for (const auto& x : refs)
        if (x.name == "BSGraphics::RenderTargetManager::GetSingleton") { r = &x; break; }
    if (!r) return "shadow atlas: RenderTargetManager not in the refs";
    uint8_t* mgr = *reinterpret_cast<uint8_t**>(const_cast<uint8_t*>(img.base) + r->rva);
    if (!mgr) return "shadow atlas: RenderTargetManager not created yet";
    char out[256];
    int n = std::snprintf(out, sizeof out, "shadow atlas:");
    for (int idx : {2, 4}) {
        auto* e = reinterpret_cast<uint32_t*>(mgr + 0xC78 + 0x10 * idx);  // width, height, arraySize
        const uint32_t w = e[0], h = e[1], a = e[2];
        const bool plausible = w == h && w >= 512 && w <= 16384 && a >= 1 && a <= 16;
        if (plausible) { e[0] = size; e[1] = size; }
        n += std::snprintf(out + n, sizeof out - size_t(n), " [%d] %ux%u x%u -> %s", idx, w, h, a,
                           plausible ? std::to_string(size).c_str() : "left (not a shadow map shape)");
    }
    return out;
}

bool InstallPapyrusTiming(const Image& img, uint32_t vmVtable, float budgetOverrideMs) {
    g_budgetOverride = budgetOverrideMs;
    auto* vt = reinterpret_cast<uint64_t*>(const_cast<uint8_t*>(img.base) + vmVtable);
    DWORD old;
    if (!VirtualProtect(vt + 4, 16, PAGE_READWRITE, &old)) return false;
    g_origUpdate = reinterpret_cast<VmFn>(vt[4]);
    g_origTasklets = reinterpret_cast<VmFn>(vt[5]);
    InterlockedExchange64(reinterpret_cast<volatile LONG64*>(vt + 4), LONG64(&HookUpdate));
    InterlockedExchange64(reinterpret_cast<volatile LONG64*>(vt + 5), LONG64(&HookTasklets));
    VirtualProtect(vt + 4, 16, old, &old);
    return true;
}

}  // namespace raw::rt
