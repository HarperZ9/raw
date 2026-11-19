// HotReload: shader and parameter changes reach the running game with no restart
// (docs/HOT_RELOAD.md). Keys of [HotReload] in d3d11_proxy.ini:
//   Enabled=1        watch the shader folder and this ini
//   ShaderDir=...    the folder with Lighting.hlsl, LightFog.hlsl and LightingCommon.hlsli
//   Dxc=...          dxcompiler.dll (default: beside the game, then the Windows SDK)
// Every 15 presents the newest write time in ShaderDir is read. A change starts a compile on a
// worker thread with the build's own flags; at a later present the new pipelines are swapped in.
// A failed compile keeps the old pipelines, and the compiler's text goes to the log, to
// raw_hot_reload.txt beside the game and to the on-screen overlay (Overlay.cpp). A change of the
// ini reloads [Lighting] and applies it at once. Original work, MIT.
#include "BridgeHookState.h"
#include "HotReload.h"
#include "Overlay.h"
#include "ProxyLog.h"
#include "../d3d12_bridge/LightingPass.h"
#include "../d3d12_bridge/ShaderCompiler.h"

#include <chrono>
#include <filesystem>
#include <future>
#include <string>

namespace SB::Proxy
{
namespace
{
namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

struct Compiled { raw::bridge::CompileResult vs, fs, fog, rt, sky, water, sss; bool hasRt = false, hasWater = false; Clock::time_point seen; };

struct State
{
    bool enabled = false;
    std::string ini;
    std::wstring dir, dxc;
    fs::file_time_type shaderTime{}, iniTime{};
    std::future<Compiled> job;
    uint64_t presents = 0;
    uint32_t reloads = 0, failures = 0, paramReloads = 0;
    double lastMs = 0;
    std::string lastError;
};
State& S() { static State s; return s; }

fs::file_time_type Newest(const std::wstring& dir)
{
    fs::file_time_type t{};
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        const auto x = e.path().extension().wstring();
        if (x == L".hlsl" || x == L".hlsli") t = std::max(t, e.last_write_time(ec));
    }
    return t;
}

void Status()
{
    State& s = S();
    char path[MAX_PATH]{};
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    std::string p(path);
    p = p.substr(0, p.rfind('\\') + 1) + "raw_hot_reload.txt";
    FILE* f = nullptr;
    if (fopen_s(&f, p.c_str(), "w") != 0 || !f) return;
    fprintf(f, "reloads=%u\nfailures=%u\nparams=%u\nlast_ms=%.1f\nerror=%s\n", s.reloads, s.failures, s.paramReloads, s.lastMs,
            s.lastError.empty() ? "" : s.lastError.substr(0, s.lastError.find('\n')).c_str());
    fclose(f);
}

void Start(State& s)
{
    const std::wstring dir = s.dir, dxc = s.dxc;
    const Clock::time_point seen = Clock::now();
    s.job = std::async(std::launch::async, [dir, dxc, seen] {
        Compiled c;
        c.seen = seen;
        c.vs = raw::bridge::CompileHlsl(dir + L"\\Lighting.hlsl", L"vs", L"vs_6_0", dxc);
        c.fs = raw::bridge::CompileHlsl(dir + L"\\Lighting.hlsl", L"fs", L"ps_6_0", dxc);
        c.fog = raw::bridge::CompileHlsl(dir + L"\\LightFog.hlsl", L"fs", L"ps_6_0", dxc);
        std::error_code ec;
        c.hasRt = fs::exists(dir + L"\\RtShadow.hlsl", ec) && fs::exists(dir + L"\\RtSkyFilter.hlsl", ec);
        if (c.hasRt) {
            c.rt = raw::bridge::CompileHlsl(dir + L"\\RtShadow.hlsl", L"main", L"cs_6_6", dxc);
            c.sky = raw::bridge::CompileHlsl(dir + L"\\RtSkyFilter.hlsl", L"main", L"cs_6_6", dxc);
        }
        c.hasWater = c.hasRt && fs::exists(dir + L"\\RtWater.hlsl", ec) && fs::exists(dir + L"\\Sss.hlsl", ec);
        if (c.hasWater) {
            c.water = raw::bridge::CompileHlsl(dir + L"\\RtWater.hlsl", L"water", L"cs_6_6", dxc);
            c.sss = raw::bridge::CompileHlsl(dir + L"\\Sss.hlsl", L"main", L"cs_6_6", dxc);
        }
        return c;
    });
}

void Finish(State& s)
{
    Compiled c = s.job.get();
    std::string err;
    for (const raw::bridge::CompileResult* r : {&c.vs, &c.fs, &c.fog, &c.rt, &c.sky, &c.water, &c.sss})
        if ((r == &c.rt || r == &c.sky) && !c.hasRt) continue;
        else if ((r == &c.water || r == &c.sss) && !c.hasWater) continue;
        else if (!r->ok) {
            const std::string e = r->errors.empty() ? std::string("compile failed\n") : r->errors;
            if (err.find(e) == std::string::npos) err += e;   // the vertex and pixel entries share a file
        }
    bool ok = err.empty();
    if (ok) {
        raw::bridge::D3D12Bridge* b = hook::Bridge();
        ok = b && b->Ready() && raw::bridge::LightingPassReload(b->ActivePass(), c.vs.dxil, c.fs.dxil, c.fog.dxil, err);
        if (!ok && err.empty()) err = "the bridge is not running the lighting pass";
        // The ray-tracing shaders reload with them when the scene runs (RtShadows on raw::tx).
        std::string rtErr;
        if (ok && c.hasRt && !b->ReloadRt(c.rt.dxil, c.sky.dxil, rtErr) && rtErr.find("no ray-tracing") == std::string::npos) {
            ok = false;
            err = rtErr;
        }
        if (ok && c.hasWater && !b->ReloadWater(c.water.dxil, c.sss.dxil, rtErr) && rtErr.find("no ray-tracing") == std::string::npos) {
            ok = false;
            err = rtErr;
        }
    }
    s.lastMs = std::chrono::duration<double, std::milli>(Clock::now() - c.seen).count();
    if (ok) {
        ++s.reloads;
        s.lastError.clear();
        Overlay_Set("");
        Log("[HotReload] shaders reloaded in %.0f ms (compile %.0f + %.0f + %.0f ms), reload %u", s.lastMs, c.vs.ms, c.fs.ms, c.fog.ms, s.reloads);
    } else {
        ++s.failures;
        s.lastError = err;
        Overlay_Set("RAW hot reload: the shader did not compile; the last good one stays.\n" + err);
        Log("[HotReload] reload failed, the old pipelines stay:\n%s", err.c_str());
    }
    Status();
}
} // namespace

void HotReload_LoadConfig(const std::string& ini)
{
    State& s = S();
    s.ini = ini;
    s.enabled = GetPrivateProfileIntA("HotReload", "Enabled", 0, ini.c_str()) != 0;
    wchar_t w[MAX_PATH]{};
    const std::wstring wini(ini.begin(), ini.end());
    GetPrivateProfileStringW(L"HotReload", L"ShaderDir", L"", w, MAX_PATH, wini.c_str());
    s.dir = w;
    GetPrivateProfileStringW(L"HotReload", L"Dxc", L"", w, MAX_PATH, wini.c_str());
    s.dxc = w;
    std::error_code ec;
    if (s.enabled) {
        s.shaderTime = s.dir.empty() ? fs::file_time_type{} : Newest(s.dir);
        s.iniTime = fs::last_write_time(ini, ec);
        Log("[HotReload] watching %ls and the ini", s.dir.empty() ? L"(no shader folder)" : s.dir.c_str());
        Status();
    }
}

void HotReload_OnPresent()
{
    State& s = S();
    if (!s.enabled) return;
    if (s.job.valid() && s.job.wait_for(std::chrono::seconds(0)) == std::future_status::ready) Finish(s);
    if (++s.presents % 15 != 0) return;
    std::error_code ec;
    const auto it = fs::last_write_time(s.ini, ec);
    if (!ec && it != s.iniTime) {
        s.iniTime = it;
        hook::LoadLightingConfig(s.ini);
        hook::LightingApplyTuning();
        ++s.paramReloads;
        Log("[HotReload] [Lighting] reloaded from the ini (%u)", s.paramReloads);
        Status();
    }
    if (s.dir.empty() || s.job.valid()) return;
    const auto t = Newest(s.dir);
    if (t != s.shaderTime) {
        s.shaderTime = t;
        Start(s);
    }
}

} // namespace SB::Proxy
