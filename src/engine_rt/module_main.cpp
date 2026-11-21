// raw_engine.dll: the RAW engine runtime, loaded by the proxy from [Modules] Load. Original work, MIT.
//
// d3d11_proxy.ini, [EngineRT]:
//   CallGraph=1        decode the game's code once in a background thread (rt_callgraph.cpp)
//   VTrace=1           trace the vtables listed in engine_rt_vtrace.tsv (rt_vtrace.cpp)
//   GpuTiming=1       GPU timestamps per pass, reported by every measure (rt_passes.cpp)
//   Weather=1         Sky and TESWeather read each present, fed to the proxy's PG_SetWeatherState (rt_weather.cpp)
//   LiveState=1       lights, the view and the shader manager's state each present (rt_livestate.cpp, rt_view.cpp)
//   Emitters=1        glowing effect and particle draws join the light list (rt_emitters.cpp)
//   OutDir=raw_engine_out
// Inputs beside the game: raw_versions\skyrimse-<version>.tsv, the address table for each supported runtime
// (rt_version.h; data/versions in the repository), and engine_rt_vtrace.tsv (class, vtable rva, slot count) for VTrace.
// The running exe picks its table by file version, PE time stamp and image size; with no match nothing is hooked.
#include <windows.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>

#include "ProxyAPI.h"
#include "rt_callgraph.h"
#include "rt_control.h"
#include "rt_papyrus.h"
#include "rt_passes.h"
#include "rt_vtrace.h"
#include "rt_livestate.h"
#include "rt_view.h"
#include "rt_weather.h"
#include "rt_version.h"
#include "rt_patch.h"

namespace {

raw::rt::Image g_img;
std::string g_dir, g_out;
std::vector<raw::rt::RefTarget> g_refs;

std::string GameDir() {
    char p[MAX_PATH];
    GetModuleFileNameA(nullptr, p, MAX_PATH);
    std::string s(p);
    return s.substr(0, s.rfind('\\') + 1);
}

int IniInt(const char* key, int def) {
    return GetPrivateProfileIntA("EngineRT", key, def, (g_dir + "d3d11_proxy.ini").c_str());
}

void Log(const char* fmt, ...) {
    FILE* f = std::fopen((g_out + "\\engine_rt.log").c_str(), "a");
    if (!f) return;
    va_list a;
    va_start(a, fmt);
    std::vfprintf(f, fmt, a);
    va_end(a);
    std::fputc('\n', f);
    std::fclose(f);
}

uint32_t Hex(const std::string& s) { return uint32_t(std::stoul(s, nullptr, 16)); }

std::vector<raw::rt::VTraceClass> LoadVTrace() {
    std::vector<raw::rt::VTraceClass> out;
    std::ifstream in(g_dir + "engine_rt_vtrace.tsv");
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream ss(line);
        std::string name, vt, n;
        if (!std::getline(ss, name, '\t') || !std::getline(ss, vt, '\t') || !std::getline(ss, n)) continue;
        if (vt.rfind("0x", 0) != 0) continue;
        out.push_back({name, Hex(vt), uint32_t(std::stoul(n))});
    }
    return out;
}

void OnPresent(ID3D11DeviceContext* ctx, IDXGISwapChain* sc) {
    raw::rt::HavokFixOnPresent();
    raw::rt::VTraceOnPresent();
    raw::rt::ControlOnPresent(ctx, sc);
}

}  // namespace

extern "C" __declspec(dllexport) void RAW_ModuleStart(SB::Proxy::ProxyInterface* proxy) {
    g_dir = GameDir();
    char outName[MAX_PATH];
    GetPrivateProfileStringA("EngineRT", "OutDir", "raw_engine_out", outName, MAX_PATH, (g_dir + "d3d11_proxy.ini").c_str());
    g_out = g_dir + outName;
    CreateDirectoryA(g_out.c_str(), nullptr);
    if (!g_img.Load()) { Log("image: no code section or no .pdata"); return; }
    Log("image: text 0x%X-0x%X, %zu pdata functions", g_img.textBegin, g_img.textEnd, g_img.funcs.size());
    std::string why;
    if (!raw::rt::LoadVersionTable(g_img, g_dir, &why)) { Log("version: %s", why.c_str()); return; }
    Log("version: %s", why.c_str());
    g_refs = raw::rt::VersionRefs();
    Log("refs: %zu settings and globals from the table", g_refs.size());
    const std::string ini = g_dir + "d3d11_proxy.ini";
    // A live reload (ModuleHost.h in the proxy) starts this copy in a running game: generation above 0.
    const uint32_t generation = proxy && proxy->version >= 3 ? proxy->moduleGeneration : 0;
    Log("start: generation %u%s", generation, generation ? " (live reload: startup-only settings left as they are)" : "");
    // Startup settings land before the renderer creates its targets (this runs inside the game's
    // first D3D11CreateDevice), so iShadowMapResolution and iNumSplits size the shadow atlas. In a reload the
    // targets exist already, so they are not touched again.
    if (!generation) {
        Log("startup settings: %zu applied", raw::rt::ApplyStartupSettings(g_img, g_refs, ini, g_out + "\\engine_rt.log"));
        if (const int atlas = IniInt("ShadowAtlas", 0); atlas > 0)
            Log("%s", raw::rt::ResizeShadowAtlas(g_img, g_refs, uint32_t(atlas)).c_str());
    }
    if (IniInt("EngineTuning", 1)) Log("%s", raw::rt::ApplyEngineTuning(g_img, g_refs).c_str());
    if (IniInt("HavokFix", 1)) Log("%s", raw::rt::HavokFixInit(g_img, g_refs).c_str());
    raw::rt::ControlInit(g_img, &g_refs, g_dir, g_out, proxy ? proxy->InvalidateStateCache : nullptr);
    // GPU time per pass and the shadow atlas's viewports (rt_passes.cpp); GpuTiming=0 keeps the draw counts only
    raw::rt::ControlSetProxy(proxy);
    raw::rt::PassesInit(g_img, &g_refs, proxy, IniInt("GpuTiming", 1) != 0);
    Log("passes: proxy API version %u, viewport hook %s", proxy ? proxy->version : 0u,
        proxy && proxy->version >= 2 ? "on" : "off (proxy too old)");
    if (IniInt("VTrace", 0)) {
        const size_t n = raw::rt::InstallVTrace(g_img, LoadVTrace(), g_out + "\\vt_trace.tsv");
        Log("vtrace: %zu slots traced", n);
    }
    if (IniInt("PapyrusTiming", 0)) {
        char b[64];
        GetPrivateProfileStringA("EngineRT", "PapyrusBudgetMs", "-1", b, sizeof b, ini.c_str());
        char v[64];
        char dv[24], dl[24];
        std::snprintf(dv, sizeof dv, "0x%X", raw::rt::Addr("vtable:BSScript::Internal::VirtualMachine"));
        std::snprintf(dl, sizeof dl, "0x%X", raw::rt::Addr("fn:BSScript::Internal::CodeTasklet::OpcodeLoop"));
        GetPrivateProfileStringA("EngineRT", "VmVtable", dv, v, sizeof v, ini.c_str());
        const bool ok = Hex(v) && raw::rt::InstallPapyrusTiming(g_img, Hex(v), std::strtof(b, nullptr));
        Log("papyrus timing on VirtualMachine vtable %s: %s, budget override %s ms", v, ok ? "on" : "failed", b);
        // the opcode loop (engine map: the 36-entry jump table under UpdateTasklets)
        char l[64];
        GetPrivateProfileStringA("EngineRT", "OpcodeLoop", dl, l, sizeof l, ini.c_str());
        if (IniInt("OpcodeLoopTiming", 0)) {
            std::string sites;
            const size_t n = Hex(l) ? raw::rt::InstallOpcodeLoopTiming(g_img, Hex(v), Hex(l), &sites) : 0;
            Log("opcode loop %s timed at %zu call sites: %s", l, n, sites.c_str());
        }
    }
    // weather for the ray-tracing worker (rt_weather.h); Weather=0 turns the per-frame read off
    if (IniInt("Weather", 1)) {
        raw::rt::WeatherInit(g_img);
        Log("weather: Sky read each present, PG_SetWeatherState %s",
            GetProcAddress(GetModuleHandleA("d3d11.dll"), "PG_SetWeatherState") ? "exported by the proxy" : "not exported (values recorded only)");
    }
    // the scene's lights and the shader manager's state each present (rt_livestate.h), exported as RAW_EngineLights
    // with the view (camera, cell, time, screen; RAW_EngineView) and the glowing emitters (rt_emitters.h)
    if (IniInt("LiveState", 1)) { raw::rt::LiveStateInit(g_img, g_out, IniInt("EmitterLights", 1) != 0); raw::rt::ViewInit(g_img); }
    if (proxy && proxy->RegisterPrePresent) proxy->RegisterPrePresent(&OnPresent);
    if (const std::string m = raw::rt::MissingEntries(); !m.empty()) Log("version: not in the table, left off: %s", m.c_str());
    if (IniInt("CallGraph", 0)) {
        raw::rt::ThreadBegin();   // a module stop refuses while the call graph runs (rt_patch.h)
        std::thread([] {
            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
            const auto& refs = g_refs;
            const auto st = raw::rt::RunCallGraph(g_img, refs, g_out);
            Log("callgraph: %llu functions, %llu leaf, %llu instructions, %llu edges, %llu vcalls, %llu refs (of %zu targets), "
                "%llu clamp sites, %llu jump tables, %.1f ms",
                st.functions, st.leafFunctions, st.instructions, st.edges, st.vcalls, st.refs, refs.size(), st.clampSites,
                st.jumpTables, st.ms);
            std::FILE* done = std::fopen((g_out + "\\cg_done.txt").c_str(), "w");
            if (done) { std::fputs("done\n", done); std::fclose(done); }
            raw::rt::ThreadEnd();
        }).detach();
    }
}

// Before a live reload (ModuleHost.h in the proxy): every patch this copy made in the game's code and vtables is
// put back, newest first. The proxy then drops this copy's callbacks and frees it later. Refused (0) while a
// background thread of the module runs; nothing is changed then.
extern "C" __declspec(dllexport) int RAW_ModuleStop(char* why, unsigned n) {
    if (const int t = raw::rt::ThreadsRunning(); t > 0) {
        std::snprintf(why, n, "%d background thread(s) running (sample or call graph); ask again when they finish", t);
        return 0;
    }
    std::string failures;
    const size_t had = raw::rt::PatchCount(), restored = raw::rt::PatchRestoreAll(&failures);
    std::snprintf(why, n, "%zu of %zu patches restored%s%s", restored, had, failures.empty() ? "" : "; ", failures.c_str());
    Log("stop: %s", why);
    return 1;
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(h);
    return TRUE;
}
