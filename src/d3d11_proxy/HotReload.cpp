// HotReload: shader and parameter changes reach the running game with no restart
// (docs/HOT_RELOAD.md). Keys of [HotReload] in d3d11_proxy.ini:
//   Enabled=1        watch the shader folder and this ini
//   ShaderDir=...    the folder with Lighting.hlsl, LightFog.hlsl and LightingCommon.hlsli (and Tonemap.hlsl, Tonemap.cpp)
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
#include "Tonemap.h"
#include "../d3d12_bridge/LightingPass.h"
#include "../d3d12_bridge/ShaderCompiler.h"

#include <chrono>
#include <filesystem>
#include <future>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace SB::Proxy
{
namespace
{
namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

struct Compiled {
    raw::bridge::CompileResult vs, fs, fog, rt, sky, water, sss;
    raw::bridge::CompileResult clouds, volumetric, bloomPre, bloomDown, bloomUp, bloomComp, reflFilter, rawlss5;
    raw::bridge::CompileResult dofFocus, dofCoc, dofGather, dofPost, dofComp;
    raw::bridge::CompileResult froxelInject, froxelBlock, froxelInt;
    raw::bridge::CompileResult giAt1, giAt2, giAt4, worldSim;
    bool hasLight = false, hasFog = false, hasRt = false, hasSky = false, hasWater = false, hasSss = false;
    bool hasClouds = false, hasVol = false, hasBloom = false, hasReflFilt = false, hasRawlss5 = false, hasDof = false, hasFroxel = false;
    bool hasGiAtrous = false, hasWorldSim = false;
    Clock::time_point seen;
};

struct State
{
    bool enabled = false;
    std::string ini;
    std::wstring dir, dxc;
    fs::file_time_type shaderTime{}, iniTime{};
    std::unordered_map<std::wstring, fs::file_time_type> fileTimes;
    std::future<Compiled> job;
    uint64_t presents = 0;
    uint32_t reloads = 0, failures = 0, paramReloads = 0;
    double lastMs = 0;
    std::string lastError;
};
State& S() { static State s; return s; }

fs::file_time_type Newest(const std::wstring& dir, std::unordered_map<std::wstring, fs::file_time_type>* times = nullptr)
{
    fs::file_time_type t{};
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        const auto x = e.path().extension().wstring();
        if (x == L".hlsl" || x == L".hlsli" || x == L".glsl" || x == L".comp" || x == L".frag" || x == L".vert") {
            const auto wt = e.last_write_time(ec);
            t = std::max(t, wt);
            if (times) (*times)[e.path().filename().wstring()] = wt;
        }
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

void Start(State& s, std::unordered_set<std::wstring> changed, bool allShaders)
{
    const std::wstring dir = s.dir, dxc = s.dxc;
    const Clock::time_point seen = Clock::now();
    s.job = std::async(std::launch::async, [dir, dxc, seen, changed = std::move(changed), allShaders] {
        Compiled c;
        c.seen = seen;
        auto want = [&](const wchar_t* name) {
            std::error_code ec;
            const std::wstring wName(name);
            const std::wstring stem = fs::path(wName).stem().wstring();
            const std::wstring glslName = stem + L".glsl";
            const bool hlslHit = (allShaders || changed.count(wName) > 0) && fs::exists(dir + L"\\" + wName, ec);
            const bool glslHit = (allShaders || changed.count(glslName) > 0) && fs::exists(dir + L"\\" + glslName, ec);
            return hlslHit || glslHit;
        };
        auto resolveShader = [&](const std::wstring& hlslPath) {
            std::error_code ec;
            fs::path p(hlslPath);
            fs::path g = p;
            g.replace_extension(L".glsl");
            if (fs::exists(g, ec)) {
                if (!fs::exists(p, ec) || fs::last_write_time(g, ec) >= fs::last_write_time(p, ec)) return g.wstring();
            }
            return hlslPath;
        };
        auto comp = [&](const std::wstring& file, const wchar_t* entry, const wchar_t* sm66) {
            return raw::bridge::CompileShaderAuto(resolveShader(file), entry, sm66, dxc);
        };
        c.hasLight = want(L"Lighting.hlsl");
        if (c.hasLight) {
            c.vs = comp(dir + L"\\Lighting.hlsl", L"vs", L"vs_6_6");
            c.fs = comp(dir + L"\\Lighting.hlsl", L"fs", L"ps_6_6");
        }
        c.hasFog = want(L"LightFog.hlsl");
        if (c.hasFog) c.fog = comp(dir + L"\\LightFog.hlsl", L"fs", L"ps_6_6");
        c.hasRt = want(L"RtShadow.hlsl");
        if (c.hasRt) c.rt = comp(dir + L"\\RtShadow.hlsl", L"main", L"cs_6_6");
        c.hasSky = want(L"RtSkyFilter.hlsl");
        if (c.hasSky) c.sky = comp(dir + L"\\RtSkyFilter.hlsl", L"main", L"cs_6_6");
        c.hasWater = want(L"RtWater.hlsl");
        if (c.hasWater) c.water = comp(dir + L"\\RtWater.hlsl", L"water", L"cs_6_6");
        c.hasSss = want(L"Sss.hlsl");
        if (c.hasSss) c.sss = comp(dir + L"\\Sss.hlsl", L"main", L"cs_6_6");
        c.hasClouds = want(L"Clouds.hlsl");
        if (c.hasClouds) c.clouds = comp(dir + L"\\Clouds.hlsl", L"fs", L"ps_6_6");
        c.hasVol = want(L"VolumetricPass.hlsl");
        if (c.hasVol) c.volumetric = comp(dir + L"\\VolumetricPass.hlsl", L"fs", L"ps_6_6");
        c.hasBloom = want(L"BloomPass.hlsl");
        if (c.hasBloom) {
            c.bloomPre  = comp(dir + L"\\BloomPass.hlsl", L"prefilter", L"ps_6_6");
            c.bloomDown = comp(dir + L"\\BloomPass.hlsl", L"down",      L"ps_6_6");
            c.bloomUp   = comp(dir + L"\\BloomPass.hlsl", L"up",        L"ps_6_6");
            c.bloomComp = comp(dir + L"\\BloomPass.hlsl", L"composite", L"ps_6_6");
        }
        c.hasReflFilt = want(L"RtReflFilter.hlsl");
        if (c.hasReflFilt) c.reflFilter = comp(dir + L"\\RtReflFilter.hlsl", L"fs", L"ps_6_6");
        c.hasRawlss5 = want(L"Rawlss5Pass.hlsl");
        if (c.hasRawlss5) c.rawlss5 = comp(dir + L"\\Rawlss5Pass.hlsl", L"rawlss5_fs", L"ps_6_6");
        c.hasDof = want(L"DofPass.hlsl");
        if (c.hasDof) {
            c.dofFocus  = comp(dir + L"\\DofPass.hlsl", L"dof_focus",  L"ps_6_6");
            c.dofCoc    = comp(dir + L"\\DofPass.hlsl", L"dof_coc",    L"ps_6_6");
            c.dofGather = comp(dir + L"\\DofPass.hlsl", L"dof_gather", L"ps_6_6");
            c.dofPost   = comp(dir + L"\\DofPass.hlsl", L"dof_post",   L"ps_6_6");
            c.dofComp   = comp(dir + L"\\DofPass.hlsl", L"dof_comp",   L"ps_6_6");
        }
        c.hasFroxel = want(L"FroxelPass.hlsl");
        if (c.hasFroxel) {
            c.froxelInject = raw::bridge::CompileHlsl(dir + L"\\FroxelPass.hlsl", L"froxel_inject",     L"ps_6_6", dxc);
            c.froxelBlock  = raw::bridge::CompileHlsl(dir + L"\\FroxelPass.hlsl", L"froxel_block_scan", L"ps_6_6", dxc, L"FROXEL_BLOCK=1");
            c.froxelInt    = raw::bridge::CompileHlsl(dir + L"\\FroxelPass.hlsl", L"froxel_integrate",  L"ps_6_6", dxc, L"FROXEL_INT=1");
        }
        c.hasGiAtrous = want(L"GiAtrousPass.hlsl");
        if (c.hasGiAtrous) {
            c.giAt1 = comp(dir + L"\\GiAtrousPass.hlsl", L"step1", L"ps_6_6");
            c.giAt2 = comp(dir + L"\\GiAtrousPass.hlsl", L"step2", L"ps_6_6");
            c.giAt4 = comp(dir + L"\\GiAtrousPass.hlsl", L"step4", L"ps_6_6");
        }
        c.hasWorldSim = want(L"WorldFieldSim.hlsl");
        if (c.hasWorldSim) c.worldSim = comp(dir + L"\\WorldFieldSim.hlsl", L"fs", L"ps_6_6");
        return c;
    });
}

void Finish(State& s)
{
    Compiled c = s.job.get();
    std::string err;
    auto checkRes = [&](const raw::bridge::CompileResult& r, bool enabled) {
        if (!enabled || r.ok) return;
        const std::string e = r.errors.empty() ? std::string("compile failed\n") : r.errors;
        if (err.find(e) == std::string::npos) err += e;
    };
    checkRes(c.vs, c.hasLight); checkRes(c.fs, c.hasLight); checkRes(c.fog, c.hasFog);
    checkRes(c.rt, c.hasRt); checkRes(c.sky, c.hasSky);
    checkRes(c.water, c.hasWater); checkRes(c.sss, c.hasSss);
    checkRes(c.clouds, c.hasClouds); checkRes(c.volumetric, c.hasVol);
    checkRes(c.bloomPre, c.hasBloom); checkRes(c.bloomDown, c.hasBloom);
    checkRes(c.bloomUp, c.hasBloom); checkRes(c.bloomComp, c.hasBloom);
    checkRes(c.reflFilter, c.hasReflFilt); checkRes(c.rawlss5, c.hasRawlss5);
    checkRes(c.dofFocus, c.hasDof); checkRes(c.dofCoc, c.hasDof);
    checkRes(c.dofGather, c.hasDof); checkRes(c.dofPost, c.hasDof); checkRes(c.dofComp, c.hasDof);
    checkRes(c.froxelInject, c.hasFroxel); checkRes(c.froxelBlock, c.hasFroxel); checkRes(c.froxelInt, c.hasFroxel);
    checkRes(c.giAt1, c.hasGiAtrous); checkRes(c.giAt2, c.hasGiAtrous); checkRes(c.giAt4, c.hasGiAtrous);
    checkRes(c.worldSim, c.hasWorldSim);
    bool ok = err.empty();
    if (ok) {
        raw::bridge::ExtraShaders ex;
        if (c.hasClouds)   ex.clouds = c.clouds.dxil;
        if (c.hasVol)      ex.volumetric = c.volumetric.dxil;
        if (c.hasBloom)    { ex.bloomPre = c.bloomPre.dxil; ex.bloomDown = c.bloomDown.dxil; ex.bloomUp = c.bloomUp.dxil; ex.bloomComp = c.bloomComp.dxil; }
        if (c.hasReflFilt) ex.reflFilter = c.reflFilter.dxil;
        if (c.hasRawlss5)  ex.rawlss5 = c.rawlss5.dxil;
        if (c.hasDof)      { ex.dofFocus = c.dofFocus.dxil; ex.dofCoc = c.dofCoc.dxil; ex.dofGather = c.dofGather.dxil; ex.dofPost = c.dofPost.dxil; ex.dofComp = c.dofComp.dxil; }
        if (c.hasFroxel)   { ex.froxelInject = c.froxelInject.dxil; ex.froxelBlock = c.froxelBlock.dxil; ex.froxelInt = c.froxelInt.dxil; }
        if (c.hasGiAtrous) { ex.giAt1 = c.giAt1.dxil; ex.giAt2 = c.giAt2.dxil; ex.giAt4 = c.giAt4.dxil; }
        if (c.hasWorldSim) ex.worldSim = c.worldSim.dxil;
        raw::bridge::D3D12Bridge* b = hook::Bridge();
        const bool anyRaster = c.hasLight || c.hasFog || c.hasClouds || c.hasVol || c.hasBloom || c.hasReflFilt || c.hasRawlss5 || c.hasDof || c.hasFroxel || c.hasGiAtrous || c.hasWorldSim;
        if (anyRaster) {
            ok = b && b->Ready() && raw::bridge::LightingPassReload(b->ActivePass(), c.vs.dxil, c.fs.dxil, c.fog.dxil, err, &ex);
            if (!ok && err.empty()) err = "the bridge is not running the lighting pass";
        }
        std::string rtErr;
        if (ok && (c.hasRt || c.hasSky) && !b->ReloadRt(c.rt.dxil, c.sky.dxil, rtErr) && rtErr.find("no ray-tracing") == std::string::npos) {
            ok = false;
            err = rtErr;
        }
        if (ok && (c.hasWater || c.hasSss) && !b->ReloadWater(c.water.dxil, c.sss.dxil, rtErr) && rtErr.find("no ray-tracing") == std::string::npos) {
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
        s.shaderTime = s.dir.empty() ? fs::file_time_type{} : Newest(s.dir, &s.fileTimes);
        s.iniTime = fs::last_write_time(ini, ec);
        if (!s.dir.empty()) Tonemap_SetSource(s.dir + L"\\Tonemap.hlsl");   // RAW's tone map reloads from the same folder
        Log("[HotReload] watching %ls and the ini", s.dir.empty() ? L"(no shader folder)" : s.dir.c_str());
        Status();
    }
}

void HotReload_OnPresent()
{
    State& s = S();
    // Always retire completed in-memory live shader compiles even when disk hot-reload polling is disabled.
    if (s.job.valid() && s.job.wait_for(std::chrono::seconds(0)) == std::future_status::ready) Finish(s);
    if (!s.enabled) return;
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
    std::unordered_map<std::wstring, fs::file_time_type> nowTimes;
    const auto t = Newest(s.dir, &nowTimes);
    if (t != s.shaderTime) {
        s.shaderTime = t;
        std::unordered_set<std::wstring> changed;
        bool headerChanged = false;
        for (const auto& [name, wt] : nowTimes) {
            auto prev = s.fileTimes.find(name);
            if (prev == s.fileTimes.end() || prev->second != wt) {
                changed.insert(name);
                if (name.size() > 6 && name.compare(name.size() - 6, 6, L".hlsli") == 0) headerChanged = true;
            }
        }
        s.fileTimes = std::move(nowTimes);
        Start(s, std::move(changed), headerChanged || changed.empty());
    }
}

void HotReload_SyncIniTime()
{
    State& s = S();
    if (s.ini.empty()) return;
    std::error_code ec;
    const auto it = fs::last_write_time(s.ini, ec);
    if (!ec) s.iniTime = it;
}

bool HotReload_SubmitLiveShader(const std::string& passName, const std::string& source, bool isGlsl)
{
    State& s = S();
    if (passName.empty() || source.empty() || s.job.valid()) return false;
    std::wstring dir = s.dir;
    if (dir.empty()) {
        wchar_t exe[MAX_PATH]{};
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        std::wstring p(exe);
        const size_t sl = p.rfind(L'\\');
        if (sl != std::wstring::npos) dir = p.substr(0, sl + 1) + L"raw_shaders";
    }
    const std::wstring dxc = s.dxc;
    const Clock::time_point seen = Clock::now();
    s.job = std::async(std::launch::async, [passName, source, isGlsl, dir, dxc, seen] {
        Compiled c;
        c.seen = seen;
        const std::wstring vname(passName.begin(), passName.end());
        auto compMem = [&](const wchar_t* entry, const wchar_t* target, const wchar_t* def = nullptr) {
            if (isGlsl) {
                return raw::bridge::CompileGlslFromMemory(source, vname + L".glsl", entry, target, dxc, def);
            }
            return raw::bridge::CompileHlslFromMemory(source, vname + L".hlsl", entry, target, dxc, def,
                                                      raw::bridge::ShaderOutput::Dxil, dir);
        };
        if (passName == "Lighting") {
            c.hasLight = true;
            c.vs = compMem(L"vs", L"vs_6_6");
            c.fs = compMem(L"fs", L"ps_6_6");
        } else if (passName == "LightFog") {
            c.hasFog = true; c.fog = compMem(L"fs", L"ps_6_6");
        } else if (passName == "Clouds") {
            c.hasClouds = true; c.clouds = compMem(L"fs", L"ps_6_6");
        } else if (passName == "VolumetricPass") {
            c.hasVol = true; c.volumetric = compMem(L"fs", L"ps_6_6");
        } else if (passName == "RtShadow") {
            c.hasRt = true; c.rt = compMem(L"main", L"cs_6_6");
        } else if (passName == "RtSkyFilter") {
            c.hasSky = true; c.sky = compMem(L"main", L"cs_6_6");
        } else if (passName == "RtWater") {
            c.hasWater = true; c.water = compMem(L"water", L"cs_6_6");
        } else if (passName == "Sss") {
            c.hasSss = true; c.sss = compMem(L"main", L"cs_6_6");
        } else if (passName == "RtReflFilter") {
            c.hasReflFilt = true; c.reflFilter = compMem(L"fs", L"ps_6_6");
        } else if (passName == "Rawlss5Pass") {
            c.hasRawlss5 = true; c.rawlss5 = compMem(L"rawlss5_fs", L"ps_6_6");
        } else if (passName == "DofPass") {
            c.hasDof = true;
            c.dofFocus  = compMem(L"dof_focus",  L"ps_6_6");
            c.dofCoc    = compMem(L"dof_coc",    L"ps_6_6");
            c.dofGather = compMem(L"dof_gather", L"ps_6_6");
            c.dofPost   = compMem(L"dof_post",   L"ps_6_6");
            c.dofComp   = compMem(L"dof_comp",   L"ps_6_6");
        } else if (passName == "FroxelPass") {
            c.hasFroxel = true;
            c.froxelInject = compMem(L"froxel_inject", L"ps_6_6");
            c.froxelBlock  = compMem(L"froxel_block_scan", L"ps_6_6", L"FROXEL_BLOCK=1");
            c.froxelInt    = compMem(L"froxel_integrate", L"ps_6_6", L"FROXEL_INT=1");
        } else if (passName == "GiAtrousPass") {
            c.hasGiAtrous = true;
            c.giAt1 = compMem(L"step1", L"ps_6_6");
            c.giAt2 = compMem(L"step2", L"ps_6_6");
            c.giAt4 = compMem(L"step4", L"ps_6_6");
        } else if (passName == "WorldFieldSim") {
            c.hasWorldSim = true;
            c.worldSim = compMem(L"fs", L"ps_6_6");
        }
        return c;
    });
    return true;
}

std::string HotReload_LiveStatus()
{
    const State& s = S();
    char buf[256];
    snprintf(buf, sizeof(buf), "reloads=%u fails=%u last=%.0fms glsl_sdk=%d%s",
             s.reloads, s.failures, s.lastMs, int(raw::bridge::HasVulkanGlslToolchain()),
             s.lastError.empty() ? "" : " [ERR]");
    return buf;
}

} // namespace SB::Proxy
