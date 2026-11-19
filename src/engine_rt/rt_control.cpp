// Engine control: settings, the go-file protocol and frame time. Original work, MIT.
#include "rt_control.h"

#include <windows.h>

#include <algorithm>
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
#include "rt_papyrus.h"
#include "rt_passes.h"

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
uint64_t g_presents = 0;
void (*g_invalidate)() = nullptr;

const RefTarget* Find(const std::string& name) {
    for (const auto& r : *g_refs)
        if (r.name == name) return &r;
    return nullptr;
}

void Result(const std::string& json) {
    // every line carries the present it was written at, to line results up with the proxy's own log
    std::string line = json;
    if (!line.empty() && line.back() == '}') line.insert(line.size() - 1, ",\"present\":" + std::to_string(g_presents));
    FILE* f = std::fopen((g_out + "\\control_results.jsonl").c_str(), "a");
    if (f) { std::fprintf(f, "%s\n", line.c_str()); std::fclose(f); }
}

uint32_t g_startupAtlas = 0;  // the atlas width before the first live rebuild: what the game's viewports assume

std::string Stats(const char* name, size_t frames) {
    std::vector<double> v(g_frameMs.end() - std::min(frames, g_frameMs.size()), g_frameMs.end());
    double sum = 0, mx = 0;
    for (double x : v) { sum += x; mx = std::max(mx, x); }
    std::sort(v.begin(), v.end());
    const double med = v.empty() ? 0 : v[v.size() / 2], p99 = v.empty() ? 0 : v[size_t(v.size() * 0.99)];
    char buf[512];
    std::snprintf(buf, sizeof buf,
                  "{\"cmd\":\"measure\",\"name\":\"%s\",\"frames\":%zu,\"mean_ms\":%.3f,\"median_ms\":%.3f,\"p99_ms\":%.3f,"
                  "\"max_ms\":%.3f}",
                  name, v.size(), v.empty() ? 0 : sum / double(v.size()), med, p99, mx);
    std::string out = buf;
    out.insert(out.size() - 1, PapyrusStats());
    out.insert(out.size() - 1, PassesStats());
    return out;
}

// peek <global name or 0xRVA> <off>... <bytes>: from the global's address, each offset dereferences the pointer
// held there and adds itself; the last number is how many bytes to show as 64-bit words. Game data only, read
// under structured exception handling.
static bool ReadU64(const void* p, uint64_t* v) {
    __try { *v = *static_cast<const volatile uint64_t*>(p); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
std::string Peek(const std::string& name, const std::string& rest) {
    uint64_t addr = 0;
    if (name.rfind("0x", 0) == 0) addr = uint64_t(g_img->base) + std::strtoull(name.c_str(), nullptr, 16);
    else if (const RefTarget* r = Find(name)) addr = uint64_t(g_img->base) + r->rva;
    else return "{\"cmd\":\"peek\",\"error\":\"no such global\"}";
    std::vector<uint64_t> nums;
    std::istringstream in(rest);
    for (std::string t; in >> t;) nums.push_back(std::strtoull(t.c_str(), nullptr, 0));
    if (nums.empty()) return "{\"cmd\":\"peek\",\"error\":\"no byte count\"}";
    const size_t bytes = std::min<uint64_t>(nums.back(), 512);
    nums.pop_back();
    for (uint64_t off : nums) {
        uint64_t v = 0;
        if (!ReadU64(reinterpret_cast<void*>(addr), &v)) return "{\"cmd\":\"peek\",\"error\":\"unreadable on the way\"}";
        addr = v + off;
    }
    std::string out = "{\"cmd\":\"peek\",\"name\":\"" + name + "\",\"path\":\"" + rest + "\",\"words\":[";
    char b[32];
    for (size_t i = 0; i < bytes; i += 8) {
        uint64_t v = 0;
        if (!ReadU64(reinterpret_cast<void*>(addr + i), &v)) { out += i ? ",null" : "null"; continue; }
        std::snprintf(b, sizeof b, "%s\"0x%llX\"", i ? "," : "", (unsigned long long)v);
        out += b;
    }
    return out + "]}";
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
            PapyrusReset();
            PassesReset();
            g_cmds.push_front("report " + a + " " + b);
            g_wait = std::atoi(b.c_str());
            return;
        } else if (op == "report") {
            Result(Stats(a.c_str(), size_t(std::atoi(b.c_str()))));
        } else if (op == "atlas") {
            // atlas <entry> <size>: rebuild a shadow depth target at a new size, live
            const uint32_t size = uint32_t(std::strtoul(b.c_str(), nullptr, 10));
            if (!g_startupAtlas) {
                DepthEntryViews e;
                if (ReadDepthEntry(*g_img, *g_refs, std::atoi(a.c_str()), &e)) g_startupAtlas = e.width;
            }
            std::string line = ResizeShadowAtlasLive(ctx, *g_img, *g_refs, std::atoi(a.c_str()), size);
            // the renderer's own size global (see ResizeShadowAtlas) follows the texture
            auto* global = reinterpret_cast<uint32_t*>(const_cast<uint8_t*>(g_img->base) + 0x33DCFC0);
            line += "; size global " + std::to_string(*global) + " -> " + std::to_string(size);
            *global = size;
            // The game's shadow passes keep the viewports of the startup size (measured: 4096 x 4096 after a
            // rebuild to 8192 or 2048). The fit scales them to the new atlas; "fit 0" turns it off.
            if (line.find("->") != std::string::npos) PassesSetFit(g_startupAtlas);
            if (g_invalidate) g_invalidate();
            Result("{\"cmd\":\"atlas\",\"result\":\"" + line + "\"}");
        } else if (op == "fit") {
            // fit <authored size | auto | 0>: scale the shadow passes' viewports to the atlas while it is bound
            const uint32_t n = a == "auto" ? g_startupAtlas : uint32_t(std::strtoul(a.c_str(), nullptr, 10));
            PassesSetFit(n);
            Result("{\"cmd\":\"fit\",\"authored\":" + std::to_string(n) + "}");
        } else if (op == "papyrus") {
            Result(PapyrusCommand(a, b));
        } else if (op == "viewports") {
            Result(PassesViewports());
        } else if (op == "probe") {
            Result(ProbeFunction(*g_img, uint32_t(std::strtoul(a.c_str(), nullptr, 16))));
        } else if (op == "peek") {
            Result(Peek(a, b));
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
        PapyrusOnPresent(g_frameMs.back());
    }
    g_last = now;
    ++g_presents;
    AtlasOnPresent();
    PassesOnPresent(ctx);
    if (!g_busy) {
        static unsigned tick = 0;
        if (++tick % 15 == 0) PollGoFile();
    }
    if (g_busy) Step(ctx, sc);
}

std::string ResizeShadowAtlas(const Image& img, const std::vector<RefTarget>& refs, uint32_t size) {
    // sub_14F00D0 copies iShadowMapResolution into a global at startup, before this module loads, and the
    // renderer sizes the shadow atlas from that global when it creates its targets (after this module
    // starts). Write both, the global only when it still holds the setting's value.
    constexpr uint32_t kShadowSizeGlobal = 0x33DCFC0;  // found by probe, SkyrimSE 1.6.1170
    auto* setting = [&]() -> uint32_t* {
        for (const auto& x : refs)
            if (x.name == "iShadowMapResolution:Display") return reinterpret_cast<uint32_t*>(const_cast<uint8_t*>(img.base) + x.rva + 8);
        return nullptr;
    }();
    auto* global = reinterpret_cast<uint32_t*>(const_cast<uint8_t*>(img.base) + kShadowSizeGlobal);
    char out[192];
    if (!setting) return "shadow atlas: iShadowMapResolution not in the refs";
    const uint32_t s0 = *setting, g0 = *global;
    if (g0 != s0) {
        std::snprintf(out, sizeof out, "shadow atlas: global 0x%X holds %u, setting %u; left alone", kShadowSizeGlobal, g0, s0);
        return out;
    }
    *setting = size;
    *global = size;
    std::snprintf(out, sizeof out, "shadow atlas: setting and global 0x%X %u -> %u", kShadowSizeGlobal, s0, size);
    return out;
}

}  // namespace raw::rt
