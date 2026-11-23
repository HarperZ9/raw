// Engine control: settings, the go-file protocol and frame time. Original work, MIT.
#include "rt_control.h"
#include "rt_version.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <fstream>
#include <sstream>
#include <string>

#include "rt_atlas.h"
#include "rt_capture.h"
#include "rt_forensics.h"
#include "rt_audit.h"
#include "rt_cascades.h"
#include "rt_emitters.h"
#include "rt_view.h"
#include "rt_drawstats.h"
#include "rt_grids.h"
#include "rt_livestate.h"
#include "rt_weather.h"
#include "rt_papyrus.h"
#include "rt_passes.h"
#include "rt_peek.h"
#include "rt_sampler.h"
#include "rt_vtime.h"

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
bool g_emittersAtStart = false;

const RefTarget* Find(const std::string& name) {
    for (const auto& r : *g_refs)
        if (r.name == name) return &r;
    return nullptr;
}

// the proxy's per-frame counters (previous frame's, read at each present), averaged per measure
SB::Proxy::ProxyInterface* g_proxy = nullptr;
double g_pc[9] = {}; uint64_t g_pcFrames = 0;
const char* kPcNames[9] = {"draws", "shader_changes", "rt_switches", "cb_maps", "cb_skipped", "srv_calls", "blend_calls",
                           "ds_calls", "rs_calls"};
void ProxyCountersOnPresent() {
    if (!g_proxy) return;
    const auto& o = g_proxy->optStats;
    const uint32_t v[9] = {g_proxy->drawCallsThisFrame, g_proxy->shaderChangesThisFrame, g_proxy->rtSwitchesThisFrame,
                           o.cbMapsIntercepted, o.cbUpdatesSkipped, o.srvCallsTotal, o.blendCallsTotal, o.dsCallsTotal, o.rsCallsTotal};
    for (int i = 0; i < 9; ++i) g_pc[i] += v[i];
    ++g_pcFrames;
}
std::string ProxyCounters() {
    if (!g_proxy || !g_pcFrames) return "";
    std::string out = ",\"proxy_per_frame\":{";
    char b[64];
    for (int i = 0; i < 9; ++i) {
        std::snprintf(b, sizeof b, "%s\"%s\":%.1f", i ? "," : "", kPcNames[i], g_pc[i] / double(g_pcFrames));
        out += b;
        g_pc[i] = 0;
    }
    g_pcFrames = 0;
    return out + "}";
}

std::mutex g_resultMutex;  // the sampler writes from its own thread
void Result(const std::string& json) {
    std::lock_guard<std::mutex> lock(g_resultMutex);
    // every line carries the present it was written at, to line results up with the proxy's own log
    std::string line = json;
    if (!line.empty() && line.back() == '}') line.insert(line.size() - 1, ",\"present\":" + std::to_string(g_presents));
    FILE* f = std::fopen((g_out + "\\control_results.jsonl").c_str(), "a");
    if (f) { std::fprintf(f, "%s\n", line.c_str()); std::fclose(f); }
}

// a command's address: 0x<rva> as given, or a version-table name (fn:..., vtable:..., a global), so the tests
// carry names and run on every supported version
uint32_t AddrArg(const std::string& s) {
    return s.rfind("0x", 0) == 0 ? uint32_t(std::strtoul(s.c_str(), nullptr, 16)) : Addr(s.c_str());
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
    out.insert(out.size() - 1, VTimeStats());
    out.insert(out.size() - 1, DrawStatsStats());
    out.insert(out.size() - 1, ProxyCounters());
    return out;
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
            VTimeReset();
            DrawStatsReset();
            ProxyCounters();
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
            if (const uint32_t g = Addr("global:ShadowMapSize")) {
                auto* global = reinterpret_cast<uint32_t*>(const_cast<uint8_t*>(g_img->base) + g);
                line += "; size global " + std::to_string(*global) + " -> " + std::to_string(size);
                *global = size;
            } else {
                line += "; no size global in this version's table";
            }
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
            Result(ProbeFunction(*g_img, AddrArg(a)));
        } else if (op == "vtime") {
            // vtime <0xvtable> <0xslot> <label>: CPU time of a vtable slot from now on
            std::istringstream rest(b);
            std::string slot, label;
            rest >> slot >> label;
            Result("{\"cmd\":\"vtime\",\"result\":\"" + VTimeSlot(*g_img, AddrArg(a),
                                                                      uint32_t(std::strtoul(slot.c_str(), nullptr, 16)), label) + "\"}");
        } else if (op == "drawstats") {
            // drawstats: per-pass draw, material and mesh statistics from now on (rt_drawstats.h)
            Result("{\"cmd\":\"drawstats\",\"result\":\"" + DrawStatsInstall(*g_img) + "\"}");
        } else if (op == "live" || op == "lights") {
            // live <every>: the state stream to live_state.jsonl; lights: the current light list (rt_livestate.h)
            Result(LiveStateCommand(op, a));
        } else if (op == "cascades") {
            // cascades [set|auto|preset ...]: RAW's cascade policy (rt_cascades.h)
            Result(CascadesCommand(a, b));
        } else if (op == "emitters") {
            // emitters: hook the effect and particle shaders (first use) and report the glowing draws (rt_emitters.h)
            if (a == "export") LiveStateExportEmitters(b == "1");   // emitters export <0|1>
            const std::string r = EmittersInstall(*g_img);
            std::string j = EmittersJson();
            j.insert(j.size() - 1, ",\"install\":\"" + r + "\"");
            Result(j);
        } else if (op == "view") {
            Result("{\"cmd\":\"view\"," + ViewJson() + "}");
        } else if (op == "weather") {
            Result(WeatherCommand());
        } else if (op == "audit") {
            Result(AuditCommand(*g_img));
        } else if (op == "forensics") {
            Result(ForensicsStatusJson());
        } else if (op == "grids") {
            // grids [n]: the loaded grid's array, or a new length while in an interior (rt_grids.h)
            Result(GridsCommand(*g_img, *g_refs, a));
        } else if (op == "ltime") {
            // ltime <0xsite> <label>: CPU time of the function a RIP-relative lea at site hands on
            Result("{\"cmd\":\"ltime\",\"result\":\"" + VTimeLea(*g_img, AddrArg(a), b) + "\"}");
        } else if (op == "sample") {
            // sample <seconds> [lo hi]: the profiler runs in the background and writes its own result line
            std::istringstream rest(b);
            char rb[2][16];
            std::snprintf(rb[0], 16, "0x%X", Addr("range:PapyrusCode.begin"));
            std::snprintf(rb[1], 16, "0x%X", Addr("range:PapyrusCode.end"));
            std::string lo = rb[0], hi = rb[1];
            rest >> lo >> hi;
            const bool ok = StartSampler(*g_img, std::atof(a.c_str()), uint32_t(std::strtoul(lo.c_str(), nullptr, 16)),
                                         uint32_t(std::strtoul(hi.c_str(), nullptr, 16)), &Result);
            if (!ok) Result("{\"cmd\":\"sample\",\"error\":\"already running\"}");
        } else if (op == "peek") {
            Result(Peek(*g_img, *g_refs, a, b));
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

void ControlInit(const Image& img, const std::vector<RefTarget>* refs, const std::string& gameDir, const std::string& outDir,
                 void (*invalidateStateCache)()) {
    g_img = &img; g_refs = refs; g_game = gameDir; g_out = outDir; g_invalidate = invalidateStateCache;
    QueryPerformanceFrequency(&g_freq);
    const std::string ini = gameDir + "d3d11_proxy.ini";
    CascadesInit(img, refs, ini);
    g_emittersAtStart = GetPrivateProfileIntA("EngineRT", "Emitters", 1, ini.c_str()) != 0;
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
    if (g_presents == 1) SamplerNoteRenderThread();
    if (g_presents == 1 && g_emittersAtStart) EmittersInstall(*g_img);   // the hooks need the render thread's id
    {
        std::string line;
        const uint32_t atlas = CascadesOnPresent(&line);
        if (!line.empty()) Result(line);
        if (atlas) { g_cmds.push_back("atlas 2 " + std::to_string(atlas)); g_busy = true; }
    }
    AtlasOnPresent();
    PassesOnPresent(ctx);
    VTimeOnPresent();
    DrawStatsOnPresent();
    WeatherOnPresent();
    ForensicsOnPresent();
    LiveStateOnPresent();
    ProxyCountersOnPresent();
    if (!g_busy) {
        static unsigned tick = 0;
        if (++tick % 15 == 0) PollGoFile();
    }
    if (g_busy) Step(ctx, sc);
}

void ControlSetProxy(SB::Proxy::ProxyInterface* proxy) { g_proxy = proxy; }

}  // namespace raw::rt
