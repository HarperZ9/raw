// BridgeHook, lighting form: the capture schedule. A set of steps (game, match, controls, each
// improvement alone, all of them) runs either after a number of world frames ([Lighting]
// CaptureAfter) or when the test harness asks for it (tools/harness/, the go file below). Each
// step holds its frames, then writes the backbuffer, the match statistics and the GPU times.
// Original work, MIT.
#include "BridgeHookState.h"
#include "ProxyLog.h"
#include "SceneLights.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <cstring>
#include <direct.h>
#include <vector>

namespace SB::Proxy::hook
{
using raw::bridge::LightingTuning;

namespace
{
uint64_t s_world = 0;
size_t s_step = 0;
uint32_t s_held = 0;
uint32_t s_burstIdx = 0;
raw::bridge::BridgeTimings s_sum{};
uint32_t s_timed = 0, s_probed = 0;

struct Step { const char* name; LightingTuning t; };
std::vector<Step> s_steps, s_all;

// The harness (tools/harness/run.py) asks for a capture set by writing raw_harness_go.txt beside
// the game: key=value lines scene, keyframe, steps (names from s_all, in order), hold, burst, stride, dir. The
// proxy answers with raw_harness_done.txt and one JSON line a step in <dir>\harness_results.jsonl.
struct Request { std::string scene, keyframe, dir; uint32_t burst = 1, stride = 2; bool active = false; };
Request s_req;
uint64_t s_polls = 0;

std::string GameFile(const char* name)
{
    char path[MAX_PATH]{};
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    std::string p(path);
    return p.substr(0, p.rfind('\\') + 1) + name;
}

bool PollHarness()
{
    if (s_req.active || ++s_polls % 20 != 0) return false;
    const std::string go = GameFile("raw_harness_go.txt");
    FILE* f = nullptr;
    if (fopen_s(&f, go.c_str(), "r") != 0 || !f) return false;
    char line[1024];
    std::string steps;
    Request r;
    int hold = Cfg().lightHold;
    while (fgets(line, sizeof line, f)) {
        std::string l(line);
        while (!l.empty() && (l.back() == '\n' || l.back() == '\r')) l.pop_back();
        const size_t eq = l.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = l.substr(0, eq), v = l.substr(eq + 1);
        if (k == "scene") r.scene = v; else if (k == "keyframe") r.keyframe = v; else if (k == "dir") r.dir = v;
        else if (k == "steps") steps = v; else if (k == "hold") hold = std::atoi(v.c_str());
        else if (k == "burst") r.burst = static_cast<uint32_t>(std::clamp(std::atoi(v.c_str()), 1, 16));
        else if (k == "stride") r.stride = static_cast<uint32_t>(std::clamp(std::atoi(v.c_str()), 1, 30));
    }
    fclose(f);
    DeleteFileA(go.c_str());
    s_steps.clear();
    for (size_t at = 0; at <= steps.size();) {
        const size_t end = std::min(steps.find(',', at), steps.size());
        const std::string name = steps.substr(at, end - at);
        for (const Step& st : s_all) if (name == st.name) s_steps.push_back(st);
        at = end + 1;
    }
    Cfg().lightHold = hold > 0 ? hold : 40;
    Cfg().lightCaptureDir = r.dir;
    Cfg().lightScene = r.scene + "_" + r.keyframe;
    r.active = !s_steps.empty() && !r.dir.empty();
    s_req = r;
    s_step = 0; s_held = 0; s_burstIdx = 0;
    Log("[Harness] go: scene %s keyframe %s, %zu steps, hold %d, burst %u, stride %u",
        r.scene.c_str(), r.keyframe.c_str(), s_steps.size(), Cfg().lightHold, r.burst, r.stride);
    if (!r.active) { FILE* d = nullptr; if (fopen_s(&d, GameFile("raw_harness_done.txt").c_str(), "w") == 0 && d) { fprintf(d, "%s refused\n", r.keyframe.c_str()); fclose(d); } }
    return r.active;
}

void HarnessResult(const Step& st, const std::string& stats)
{
    FILE* f = nullptr;
    if (fopen_s(&f, (s_req.dir + "\\harness_results.jsonl").c_str(), "a") != 0 || !f) return;
    const double inv = s_timed ? 1.0 / s_timed : 0.0;
    fprintf(f, "{\"scene\":\"%s\",\"keyframe\":\"%s\",\"step\":\"%s\",\"mode\":%d,\"width\":%u,\"height\":%u,"
               "\"timed_frames\":%u,\"pass_ms\":%.4f,\"ao_ms\":%.4f,\"lighting_ms\":%.4f,\"fog_ms\":%.4f,\"bridge_ms\":%.4f,"
               "\"stall_ms\":%.4f,\"sync_ms\":%.4f,\"probed_frames\":%u,\"wake_in_ms\":%.4f,\"d12_span_ms\":%.4f,\"wake_out_ms\":%.4f%s%s}\n",
            s_req.scene.c_str(), s_req.keyframe.c_str(), st.name, st.t.mode, Bridge() ? Bridge()->Config().width : 0,
            Bridge() ? Bridge()->Config().height : 0, s_timed, s_sum.passMs * inv, s_sum.pass.stageMs[0] * inv,
            s_sum.pass.stageMs[1] * inv, s_sum.pass.stageMs[2] * inv, s_sum.bridgeCostMs * inv, s_sum.stallMs * inv, s_sum.syncMs * inv,
            s_probed, s_probed ? s_sum.wakeInMs / s_probed : 0.0, s_probed ? s_sum.d12SpanMs / s_probed : 0.0,
            s_probed ? s_sum.wakeOutMs / s_probed : 0.0, stats.empty() ? "" : ",", stats.c_str());
    fclose(f);
}

void Tune() { LightingApplyTuning(); }

void TimingSample()
{
    raw::bridge::D3D12Bridge* b = Bridge();
    raw::bridge::BridgeTimings t;
    const uint64_t n = b->FramesExecuted();
    if (n < 3 || !b->ReadTimings(n - 3, false, &t)) return;
    s_sum.passMs += t.passMs; s_sum.bridgeCostMs += t.bridgeCostMs;
    s_sum.stallMs += t.stallMs; s_sum.syncMs += t.syncMs;
    if (t.probeOk) { s_sum.wakeInMs += t.wakeInMs; s_sum.d12SpanMs += t.d12SpanMs; s_sum.wakeOutMs += t.wakeOutMs; ++s_probed; }
    for (int k = 0; k < 3; ++k) s_sum.pass.stageMs[k] += t.pass.stageMs[k];
    ++s_timed;
}
} // namespace

void LightingMakeSteps()
{
    const LightingTuning base = Cfg().light;
    auto with = [&](int mode, auto edit) {
        LightingTuning t = base;
        t.mode = mode;
        t.view = 0;
        t.waterView = 0;
        edit(t);
        return t;
    };
    auto none = [](LightingTuning& t) { t.softShadows = t.ggxSpecular = t.volumetricFog = t.contactShadows = t.ao = t.rtShadows = false; };
    s_all = {
        {"game", with(0, [](LightingTuning&) {})},
        {"match", with(1, [](LightingTuning&) {})},
        {"match_cascades", with(1, [](LightingTuning& t) { t.cascades = true; })},   // control: our cascade lookup instead of the game's mask
        {"control_half_viewport", with(1, [](LightingTuning& t) { t.view = 9; })},     // control: the coverage check must fail
        {"improved_base", with(2, none)},
        {"improved_soft", with(2, [&](LightingTuning& t) { none(t); t.softShadows = true; })},
        {"improved_ggx", with(2, [&](LightingTuning& t) { none(t); t.ggxSpecular = true; })},
        {"improved_fog", with(2, [&](LightingTuning& t) { none(t); t.volumetricFog = true; })},
        {"improved_contact", with(2, [&](LightingTuning& t) { none(t); t.contactShadows = true; })},
        {"improved_ao", with(2, [&](LightingTuning& t) { none(t); t.ao = true; })},
        {"improved_rt", with(2, [&](LightingTuning& t) { none(t); t.rtShadows = true; })},
        {"improved_no_rt", with(2, [&](LightingTuning& t) { t.rtShadows = false; })},
        {"improved", with(2, [](LightingTuning&) {})},
        {"improved_no_lod", with(2, [](LightingTuning& t) { t.distantLod = false; })},   // control: distant LOD keeps the game's light
        {"match_no_env", with(1, [](LightingTuning& t) { t.envMap = false; })},           // control: no environment-map term
        {"improved_no_env", with(2, [](LightingTuning& t) { t.envMap = false; })},
        {"match_no_char", with(1, [](LightingTuning& t) { t.charLight = false; })},       // control: no character light
        {"improved_no_char", with(2, [](LightingTuning& t) { t.charLight = false; })},
        // Full-pipeline custom Lighting debug render views (flags.w = 1..36):
        {"view_normals", with(2, [](LightingTuning& t) { t.view = 1; })},
        {"view_sun_shadow", with(2, [](LightingTuning& t) { t.view = 2; })},
        {"view_ambient", with(2, [](LightingTuning& t) { t.view = 3; })},
        {"view_sky_vis", with(2, [](LightingTuning& t) { t.view = 4; })},
        {"view_gi_bounce", with(2, [](LightingTuning& t) { t.view = 5; })},
        {"view_rt_refl", with(2, [](LightingTuning& t) { t.view = 6; })},
        {"view_weather", with(2, [](LightingTuning& t) { t.view = 8; })},
        {"view_cloud_shadow", with(2, [](LightingTuning& t) { t.view = 11; })},
        {"view_classes", with(2, [](LightingTuning& t) { t.view = 12; })},
        {"view_lod_normal", with(2, [](LightingTuning& t) { t.view = 14; })},            // distant LOD: its G-buffer normal
        {"view_lod_albedo", with(2, [](LightingTuning& t) { t.view = 15; })},            // distant LOD: its G-buffer base colour
        {"view_albedo", with(2, [](LightingTuning& t) { t.view = 16; })},
        {"view_char_light", with(2, [](LightingTuning& t) { t.view = 19; })},
        {"view_wireframe", with(2, [](LightingTuning& t) { t.view = 22; })},
        {"view_roughness", with(2, [](LightingTuning& t) { t.view = 24; })},
        {"view_metallic", with(2, [](LightingTuning& t) { t.view = 25; })},
        {"view_parallax", with(2, [](LightingTuning& t) { t.view = 26; })},
        {"view_gi_contrast", with(2, [](LightingTuning& t) { t.view = 27; })},
        {"view_full_classes", with(2, [](LightingTuning& t) { t.view = 28; })},
        {"view_depth_edge", with(2, [](LightingTuning& t) { t.view = 29; })},
        {"view_shadow_stab", with(2, [](LightingTuning& t) { t.view = 30; })},
        {"view_moss_mask", with(2, [](LightingTuning& t) { t.view = 31; })},
        {"view_skin_sss", with(2, [](LightingTuning& t) { t.view = 32; })},
        {"view_nan_hdr", with(2, [](LightingTuning& t) { t.view = 33; })},
        {"view_flora_trees", with(2, [](LightingTuning& t) { t.view = 34; })},
        {"view_motion_outline", with(2, [](LightingTuning& t) { t.view = 35; })},
        {"view_fire_froxel", with(2, [](LightingTuning& t) { t.view = 36; })},
        // Full-pipeline custom Water debug render views (RtWater.hlsl waterView = 1..7):
        {"water_refl", with(2, [](LightingTuning& t) { t.water = true; t.waterView = 1; })},
        {"water_caustics", with(2, [](LightingTuning& t) { t.water = true; t.waterView = 2; })},
        {"water_fresnel", with(2, [](LightingTuning& t) { t.water = true; t.waterView = 3; })},
        {"water_depth", with(2, [](LightingTuning& t) { t.water = true; t.waterView = 4; })},
        {"water_body", with(2, [](LightingTuning& t) { t.water = true; t.waterView = 6; })},
        {"water_flow_diag", with(2, [](LightingTuning& t) { t.water = true; t.waterView = 7; })},
        {"game_after", with(0, [](LightingTuning&) {})},
    };
    s_steps = s_all;
}

void LightingScheduleOnPresent(bool worldFrame)
{
    raw::bridge::D3D12Bridge* b = Bridge();
    if (Cfg().harness) {
        // A heartbeat for the harness: presents and world frames, every 30 presents. It tells the
        // harness whether the game still renders (minimised, behind other windows).
        // loads: how many times the world came back after 10 or more presents without it (a
        // loading screen), so the harness can tell that a cell change happened.
        static uint64_t presents = 0, world = 0, loads = 0, gap = 0;
        ++presents; world += worldFrame;
        if (!worldFrame) ++gap;
        else { if (gap >= 10) ++loads; gap = 0; }
        if (presents % 30 == 0) {
            FILE* f = nullptr;
            if (fopen_s(&f, GameFile("raw_harness_status.txt").c_str(), "w") == 0 && f) {
                // cell: a signature of the decoded cell lighting (sun shadows or not, ambient). A coc into
                // a cell still in memory shows no loading screen; the signature still changes.
                const raw::bridge::SceneLighting& l = SceneLights_Frame();
                const long long cell = (l.hasCascades ? 1000000000LL : 0) + (long long)(l.ambient[0][3] * 1000.0f) * 10000 +
                                       (long long)(l.ambient[2][3] * 1000.0f) * 10 + (l.hasSun ? 1 : 0);
                fprintf(f, "presents=%llu world=%llu loads=%llu cell=%lld active=%d\n", (unsigned long long)presents,
                        (unsigned long long)world, (unsigned long long)loads, cell, s_req.active ? 1 : 0);
                fclose(f);
            }
        }
    }
    if (!worldFrame) return;
    if (Cfg().harness) {
        if (!s_req.active && !PollHarness()) return;
    } else {
        if (Cfg().lightCaptureAfter <= 0 || Cfg().lightCaptureDir.empty() || s_step >= s_steps.size()) return;
        if (++s_world < static_cast<uint64_t>(Cfg().lightCaptureAfter)) return;
    }
    if (s_held == 0 && s_burstIdx == 0) {
        Cfg().light = s_steps[s_step].t;
        Tune();
        s_sum = {}; s_timed = 0; s_probed = 0;
    }
    if (b && b->Ready() && Cfg().light.mode > 0) TimingSample();
    const uint32_t burstTotal = (Cfg().harness && s_req.active) ? s_req.burst : 1u;
    const uint32_t targetHold = (s_burstIdx == 0u)
        ? static_cast<uint32_t>(Cfg().lightHold)
        : ((Cfg().harness && s_req.active) ? s_req.stride : 1u);
    if (++s_held < targetHold) return;
    const Step& st = s_steps[s_step];
    _mkdir(Cfg().lightCaptureDir.c_str());
    std::string bmpPath;
    if (burstTotal <= 1u) {
        bmpPath = Cfg().lightCaptureDir + "\\" + Cfg().lightScene + "_" + st.name + ".bmp";
    } else {
        char suffix[20];
        std::snprintf(suffix, sizeof suffix, "_f%02u.bmp", s_burstIdx);
        bmpPath = Cfg().lightCaptureDir + "\\" + Cfg().lightScene + "_" + st.name + suffix;
    }
    CaptureBackbuffer(bmpPath);
    if (s_burstIdx + 1u < burstTotal) {
        ++s_burstIdx;
        s_held = 0;
        return;
    }
    std::string stats;
    if (b && b->Ready()) LightingStats(st.name, st.t.mode, &stats);
    if (b && b->Ready()) {
        raw::bridge::RtFrameStats r;
        if (b->RtStats(&r)) {
            char x[900];
            snprintf(x, sizeof x, "%s\"rt_draws\":%u,\"rt_instances\":%u,\"rt_meshes\":%u,\"rt_skipped\":%u,\"rt_triangles\":%llu,\"rt_blas_mb\":%.1f,\"rt_build_ms\":%.4f,\"rt_trace_ms\":%.4f,"
                     "\"rt_alpha_instances\":%u,\"rt_alpha_meshes\":%u,\"rt_masks\":%u,\"rt_no_mask\":%u,\"rt_uv_mb\":%.2f,\"rt_omm_arrays\":%u,"
                     "\"rt_mat_meshes\":%u,\"rt_albedos\":%u,\"rt_no_albedo\":%u,\"rt_albedo_mb\":%.1f,\"rt_cache_cells\":%u,\"rt_cache_ms\":%.4f,\"rt_skin_draws\":%u,\"rt_skin_built\":%u,\"rt_skin_vertices\":%llu,\"rt_water_ms\":%.4f,\"rt_water_runs\":%u",
                     stats.empty() ? "" : ",", r.draws, r.instances, r.meshes, r.skipped, (unsigned long long)r.triangles, double(r.blasBytes) / 1048576.0, r.buildMs, r.traceMs,
                     r.alphaInstances, r.alphaMeshes, r.masks, r.noMask, double(r.uvBytes) / 1048576.0, r.ommArrays,
                     r.matMeshes, r.albedos, r.noAlbedo, double(r.albedoBytes) / 1048576.0, r.cacheCells, r.cacheMs, r.skinDraws, r.skinBuilt, (unsigned long long)r.skinVertices, r.waterMs, r.waterRuns);
            stats += x;
        }
    }
    if (Cfg().harness) HarnessResult(st, stats);
    if (b && b->Ready() && Cfg().lightDump && std::strcmp(st.name, "match") == 0) LightingDump();
    if (s_timed)
        Log("[Lighting] timing %s over %u frames: pass %.3f ms (ao %.3f, lighting %.3f, fog %.3f), bridge %.3f ms", st.name, s_timed,
            s_sum.passMs / s_timed, s_sum.pass.stageMs[0] / s_timed, s_sum.pass.stageMs[1] / s_timed, s_sum.pass.stageMs[2] / s_timed,
            s_sum.bridgeCostMs / s_timed);
    s_held = 0;
    s_burstIdx = 0;
    if (++s_step < s_steps.size()) return;
    Log("[Lighting] capture set done");
    if (Cfg().harness && s_req.active) {
        s_req.active = false;
        FILE* d = nullptr;
        if (fopen_s(&d, GameFile("raw_harness_done.txt").c_str(), "w") == 0 && d) { fprintf(d, "%s ok\n", s_req.keyframe.c_str()); fclose(d); }
    }
}

} // namespace SB::Proxy::hook
