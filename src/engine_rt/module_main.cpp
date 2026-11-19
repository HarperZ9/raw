// raw_engine.dll: the RAW engine runtime, loaded by the proxy from [Modules] Load. Original work, MIT.
//
// d3d11_proxy.ini, [EngineRT]:
//   CallGraph=1        decode the game's code once in a background thread (rt_callgraph.cpp)
//   VTrace=1           trace the vtables listed in engine_rt_vtrace.tsv (rt_vtrace.cpp)
//   GpuTiming=1       GPU timestamps per pass, reported by every measure (rt_passes.cpp)
//   OutDir=raw_engine_out
// Inputs beside the game: engine_rt_refs.tsv (rva, kind setting|global, name) and
// engine_rt_vtrace.tsv (class, vtable rva, slot count), both generated from the engine map.
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

std::vector<raw::rt::RefTarget> LoadRefs() {
    std::vector<raw::rt::RefTarget> out;
    std::ifstream in(g_dir + "engine_rt_refs.tsv");
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream ss(line);
        std::string rva, kind, name;
        if (!std::getline(ss, rva, '\t') || !std::getline(ss, kind, '\t') || !std::getline(ss, name)) continue;
        if (rva.rfind("0x", 0) != 0) continue;
        raw::rt::RefTarget r;
        r.rva = Hex(rva); r.setting = kind == "setting"; r.span = r.setting ? 24 : 8; r.name = name;
        out.push_back(r);
    }
    std::sort(out.begin(), out.end(), [](auto& a, auto& b) { return a.rva < b.rva; });
    return out;
}

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
    g_refs = LoadRefs();
    const std::string ini = g_dir + "d3d11_proxy.ini";
    // Startup settings land before the renderer creates its targets (this runs inside the game's
    // first D3D11CreateDevice), so iShadowMapResolution and iNumSplits size the shadow atlas.
    Log("startup settings: %zu applied", raw::rt::ApplyStartupSettings(g_img, g_refs, ini, g_out + "\\engine_rt.log"));
    if (const int atlas = IniInt("ShadowAtlas", 0); atlas > 0)
        Log("%s", raw::rt::ResizeShadowAtlas(g_img, g_refs, uint32_t(atlas)).c_str());
    raw::rt::ControlInit(g_img, &g_refs, g_dir, g_out, proxy ? proxy->InvalidateStateCache : nullptr);
    // GPU time per pass and the shadow atlas's viewports (rt_passes.cpp); GpuTiming=0 keeps the draw counts only
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
        GetPrivateProfileStringA("EngineRT", "VmVtable", "0x1AA0B48", v, sizeof v, ini.c_str());
        const bool ok = raw::rt::InstallPapyrusTiming(g_img, Hex(v), std::strtof(b, nullptr));
        Log("papyrus timing on VirtualMachine vtable %s: %s, budget override %s ms", v, ok ? "on" : "failed", b);
        // the opcode loop (engine map: the 36-entry jump table under UpdateTasklets)
        char l[64];
        GetPrivateProfileStringA("EngineRT", "OpcodeLoop", "0x1462650", l, sizeof l, ini.c_str());
        if (IniInt("OpcodeLoopTiming", 0)) {
            std::string sites;
            const size_t n = raw::rt::InstallOpcodeLoopTiming(g_img, Hex(v), Hex(l), &sites);
            Log("opcode loop %s timed at %zu call sites: %s", l, n, sites.c_str());
        }
    }
    if (proxy && proxy->RegisterPrePresent) proxy->RegisterPrePresent(&OnPresent);
    if (IniInt("CallGraph", 0)) {
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
        }).detach();
    }
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(h);
    return TRUE;
}
