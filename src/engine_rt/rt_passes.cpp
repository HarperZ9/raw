// Pass tracking: GPU time per render pass, the shadow atlas binding and its viewports (RAW engine runtime).
// Original work, MIT.
//
// Every change of pass writes a GPU timestamp on the real context. A pass is the proxy's render phase, except
// that a bound shadow atlas (depth entry 2) or depth entry 4 overrides the phase: those two are found by the
// depth view itself, so they are exact where the phase detector guesses. Frames are read four presents later
// without flushing, so the timing does not stall the game.
#include "rt_passes.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>

#include "rt_atlas.h"

namespace raw::rt {
namespace {

enum Label : uint8_t { kPhases = 9, kAtlas = 9, kDepth4 = 10, kLabels = 11 };
const char* kNames[kLabels] = {"unknown", "depth_prepass", "shadow_phase", "geometry", "decals", "sky",
                               "alpha_blend", "post", "ui", "shadow_atlas", "depth4"};

constexpr int kFrames = 5;       // frames in flight before a read
constexpr int kMarks = 1024;     // timestamps per frame; changes past this are dropped and counted

struct Frame {
    ID3D11Query* disjoint = nullptr;
    std::array<ID3D11Query*, kMarks> ts{};
    std::array<uint8_t, kMarks> label{};
    int marks = 0;
    bool open = false;
};

const Image* g_img = nullptr;
const std::vector<RefTarget>* g_refs = nullptr;
ID3D11Device* g_dev = nullptr;
ID3D11DeviceContext* g_ctx = nullptr;  // the real immediate context
bool g_timing = false;
Frame g_frames[kFrames];
int g_cur = 0;
uint8_t g_phase = 0, g_label = 0;
bool g_inAtlas = false, g_inDepth4 = false;
DepthEntryViews g_atlas, g_depth4;
uint64_t g_dropped = 0;

// accumulators since the last reset
double g_gpuMs[kLabels] = {};
double g_frameGpuMs = 0;
uint64_t g_draws[kLabels] = {};
uint64_t g_gpuFrames = 0, g_cpuFrames = 0;
uint32_t g_drawsThisFrame[kLabels] = {};

// viewports while the atlas is bound
uint32_t g_fitAuthored = 0;
D3D11_VIEWPORT g_gameVp[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE] = {};
uint32_t g_gameVpCount = 0;
bool g_vpFitted = false;
struct VpSeen { D3D11_VIEWPORT vp; uint32_t uses; };
std::vector<VpSeen> g_vpFrame, g_vpLast;
uint32_t g_atlasBinds = 0, g_atlasBindsLast = 0, g_atlasDrawsLast = 0;

bool Has(const DepthEntryViews& e, ID3D11DepthStencilView* v) {
    if (!v || !e.texture) return false;
    for (auto* x : e.views)
        if (x == v) return true;
    return false;
}

float FitScale() {
    if (!g_fitAuthored || !g_atlas.width) return 1.0f;
    return float(g_atlas.width) / float(g_fitAuthored);
}

void Scale(uint32_t n, D3D11_VIEWPORT* vp, float s) {
    for (uint32_t i = 0; i < n; ++i) {
        vp[i].TopLeftX *= s; vp[i].TopLeftY *= s; vp[i].Width *= s; vp[i].Height *= s;
    }
}

void Mark() {
    const uint8_t l = g_inAtlas ? kAtlas : g_inDepth4 ? kDepth4 : g_phase;
    if (l == g_label) return;
    g_label = l;
    Frame& f = g_frames[g_cur];
    if (!g_timing || !f.open) return;
    if (f.marks >= kMarks) { ++g_dropped; return; }
    g_ctx->End(f.ts[f.marks]);
    f.label[f.marks++] = l;
}

void RecordVp(const D3D11_VIEWPORT& v) {
    for (auto& s : g_vpFrame)
        if (!std::memcmp(&s.vp, &v, sizeof v)) { ++s.uses; return; }
    if (g_vpFrame.size() < 32) g_vpFrame.push_back({v, 1});
}

void OnTargets(uint32_t, ID3D11RenderTargetView* const*, ID3D11DepthStencilView* dsv) {
    const bool wasAtlas = g_inAtlas;
    g_inAtlas = Has(g_atlas, dsv);
    g_inDepth4 = !g_inAtlas && Has(g_depth4, dsv);
    if (g_inAtlas && !wasAtlas) {
        ++g_atlasBinds;
        if (g_gameVpCount) RecordVp(g_gameVp[0]);
    }
    // The game may set its viewport before binding the atlas: re-issue it fitted on entry, as the game set
    // it on exit. Viewports are not cached by the proxy, so the real context is the whole state.
    const float s = FitScale();
    if (g_ctx && g_gameVpCount) {
        if (g_inAtlas && !wasAtlas && s != 1.0f) {
            D3D11_VIEWPORT vp[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
            std::copy(g_gameVp, g_gameVp + g_gameVpCount, vp);
            Scale(g_gameVpCount, vp, s);
            g_ctx->RSSetViewports(g_gameVpCount, vp);
            g_vpFitted = true;
        } else if (!g_inAtlas && wasAtlas && g_vpFitted) {
            g_ctx->RSSetViewports(g_gameVpCount, g_gameVp);
            g_vpFitted = false;
        }
    }
    Mark();
}

void OnViewport(uint32_t n, D3D11_VIEWPORT* vp) {
    g_gameVpCount = std::min<uint32_t>(n, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE);
    std::copy(vp, vp + g_gameVpCount, g_gameVp);
    g_vpFitted = false;
    if (!g_inAtlas) return;
    RecordVp(vp[0]);
    if (const float s = FitScale(); s != 1.0f) { Scale(n, vp, s); g_vpFitted = true; }
}

void OnPhase(uint8_t, uint8_t now) {
    g_phase = now < kPhases ? now : 0;
    Mark();
}

void OnDraw(uint32_t, uint32_t) {
    const uint8_t l = g_inAtlas ? kAtlas : g_inDepth4 ? kDepth4 : g_phase;
    ++g_drawsThisFrame[l];
}

void ReadFrame(Frame& f) {
    if (!f.open) return;
    D3D11_QUERY_DATA_TIMESTAMP_DISJOINT dj;
    if (g_ctx->GetData(f.disjoint, &dj, sizeof dj, D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK) return;  // not done yet
    f.open = false;
    if (dj.Disjoint || !dj.Frequency || f.marks < 2) return;
    std::array<uint64_t, kMarks> t{};
    for (int i = 0; i < f.marks; ++i)
        if (g_ctx->GetData(f.ts[i], &t[i], sizeof(uint64_t), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK) return;
    const double toMs = 1000.0 / double(dj.Frequency);
    for (int i = 0; i + 1 < f.marks; ++i)
        if (t[i + 1] >= t[i]) g_gpuMs[f.label[i]] += double(t[i + 1] - t[i]) * toMs;
    g_frameGpuMs += double(t[f.marks - 1] - t[0]) * toMs;
    ++g_gpuFrames;
}

void BeginFrame() {
    Frame& f = g_frames[g_cur];
    if (f.open) ReadFrame(f);  // a frame still pending after kFrames presents is given up
    f.open = true;
    f.marks = 0;
    g_ctx->Begin(f.disjoint);
    g_ctx->End(f.ts[f.marks]);
    f.label[f.marks++] = g_label;
}

}  // namespace

void PassesInit(const Image& img, const std::vector<RefTarget>* refs, SB::Proxy::ProxyInterface* proxy, bool gpuTiming) {
    // The module starts inside the game's first D3D11CreateDevice, before the device exists: callbacks are
    // registered now, the queries are made at the first present.
    g_img = &img; g_refs = refs;
    g_timing = gpuTiming;
    if (!proxy) return;
    if (proxy->RegisterOnRTChange) proxy->RegisterOnRTChange(&OnTargets);
    if (proxy->RegisterOnPhaseChange) proxy->RegisterOnPhaseChange(&OnPhase);
    if (proxy->RegisterOnDraw) proxy->RegisterOnDraw(&OnDraw);
    if (proxy->version >= 2 && proxy->RegisterOnViewport) proxy->RegisterOnViewport(&OnViewport);
}

static bool StartTiming(ID3D11DeviceContext* ctx) {
    g_ctx = ctx;
    g_ctx->GetDevice(&g_dev);
    D3D11_QUERY_DESC dq{D3D11_QUERY_TIMESTAMP_DISJOINT, 0}, tq{D3D11_QUERY_TIMESTAMP, 0};
    for (auto& f : g_frames) {
        if (FAILED(g_dev->CreateQuery(&dq, &f.disjoint))) return false;
        for (auto& q : f.ts)
            if (FAILED(g_dev->CreateQuery(&tq, &q))) return false;
    }
    return true;
}

void PassesOnPresent(ID3D11DeviceContext* ctx) {
    if (!ctx) return;
    if (!g_ctx) {
        if (g_timing && !StartTiming(ctx)) g_timing = false;
        g_ctx = ctx;
    } else if (g_timing) {
        Frame& f = g_frames[g_cur];
        if (f.open && f.marks < kMarks) { g_ctx->End(f.ts[f.marks]); f.label[f.marks++] = g_label; }
        g_ctx->End(f.disjoint);
        for (int k = 1; k < kFrames; ++k) ReadFrame(g_frames[(g_cur + k) % kFrames]);
        g_cur = (g_cur + 1) % kFrames;
    }
    for (int l = 0; l < kLabels; ++l) { g_draws[l] += g_drawsThisFrame[l]; }
    g_atlasDrawsLast = g_drawsThisFrame[kAtlas];
    std::fill(std::begin(g_drawsThisFrame), std::end(g_drawsThisFrame), 0u);
    ++g_cpuFrames;
    g_vpLast.swap(g_vpFrame);
    g_vpFrame.clear();
    g_atlasBindsLast = g_atlasBinds;
    g_atlasBinds = 0;
    // the atlas can be rebuilt between frames (atlas command); read its views again every 8 presents
    static unsigned tick = 0;
    if (tick++ % 8 == 0) {
        DepthEntryViews a, d;
        g_atlas = ReadDepthEntry(*g_img, *g_refs, 2, &a) ? a : DepthEntryViews{};
        g_depth4 = ReadDepthEntry(*g_img, *g_refs, 4, &d) ? d : DepthEntryViews{};
    }
    if (g_timing) BeginFrame();
}

void PassesReset() {
    std::fill(std::begin(g_gpuMs), std::end(g_gpuMs), 0.0);
    std::fill(std::begin(g_draws), std::end(g_draws), 0ull);
    g_frameGpuMs = 0; g_gpuFrames = 0; g_cpuFrames = 0; g_dropped = 0;
}

std::string PassesStats() {
    std::string out = ",\"gpu_ms\":{";
    char b[96];
    const double gf = g_gpuFrames ? double(g_gpuFrames) : 1.0, cf = g_cpuFrames ? double(g_cpuFrames) : 1.0;
    for (int l = 0; l < kLabels; ++l) {
        std::snprintf(b, sizeof b, "%s\"%s\":%.4f", l ? "," : "", kNames[l], g_gpuMs[l] / gf);
        out += b;
    }
    std::snprintf(b, sizeof b, ",\"frame\":%.4f},\"draws\":{", g_frameGpuMs / gf);
    out += b;
    for (int l = 0; l < kLabels; ++l) {
        std::snprintf(b, sizeof b, "%s\"%s\":%.1f", l ? "," : "", kNames[l], double(g_draws[l]) / cf);
        out += b;
    }
    std::snprintf(b, sizeof b, "},\"gpu_frames\":%llu,\"gpu_marks_dropped\":%llu", (unsigned long long)g_gpuFrames,
                  (unsigned long long)g_dropped);
    out += b;
    PassesReset();
    return out;
}

std::string PassesViewports() {
    char b[160];
    std::snprintf(b, sizeof b, "{\"cmd\":\"viewports\",\"atlas\":[%u,%u,%u],\"depth4\":[%u,%u,%u],\"binds\":%u,\"draws\":%u,\"fit\":%u,\"vp\":[",
                  g_atlas.width, g_atlas.height, g_atlas.slices, g_depth4.width, g_depth4.height, g_depth4.slices,
                  g_atlasBindsLast, g_atlasDrawsLast, g_fitAuthored);
    std::string out = b;
    bool first = true;
    for (const auto& s : g_vpLast) {
        std::snprintf(b, sizeof b, "%s[%.1f,%.1f,%.1f,%.1f,%.2f,%.2f,%u]", first ? "" : ",", s.vp.TopLeftX, s.vp.TopLeftY,
                      s.vp.Width, s.vp.Height, s.vp.MinDepth, s.vp.MaxDepth, s.uses);
        out += b;
        first = false;
    }
    return out + "]}";
}

void PassesSetFit(uint32_t authoredSize) { g_fitAuthored = authoredSize; }
uint32_t PassesFit() { return g_fitAuthored; }

}  // namespace raw::rt
