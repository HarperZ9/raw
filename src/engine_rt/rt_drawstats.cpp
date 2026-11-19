// Draw statistics per submission pass (RAW engine runtime). Original work, MIT. See rt_drawstats.h.
#include "rt_drawstats.h"
#include "rt_version.h"

#include <windows.h>
#include <intrin.h>

#include <array>
#include <cstdio>
#include <cstring>
#include <utility>
#include <vector>

namespace raw::rt {
namespace {

using Fn = uint64_t (*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);

struct ShaderClass { const char* name; const char* vtable; };
// vtables from the version table (rt_version.h); the slots are BSShader's
constexpr ShaderClass kShaders[] = {
    {"Lighting", "vtable:BSLightingShader"}, {"Utility", "vtable:BSUtilityShader"},  {"Grass", "vtable:BSGrassShader"},
    {"Effect", "vtable:BSEffectShader"},     {"Water", "vtable:BSWaterShader"},      {"Sky", "vtable:BSSkyShader"},
    {"DistantTree", "vtable:BSDistantTreeShader"}, {"Particle", "vtable:BSParticleShader"}, {"Blood", "vtable:BSBloodSplatterShader"},
};
constexpr int kShaderCount = int(std::size(kShaders));
enum Kind : int { kTech = 0, kMat = 1, kGeo = 2 };
constexpr uint32_t kSlotOf[3] = {2, 4, 6};
constexpr uint32_t kAccumSlot = 0x2A;
constexpr int kPasses = 33;          // 32 passes in frame order, then "outside a pass"
constexpr int kHooks = kShaderCount * 3;
constexpr size_t kMeshSet = 8192;    // per pass, generation-stamped

struct Cell {
    uint64_t draws = 0, tech = 0, techRep = 0, mat = 0, matRep = 0;
    uint64_t ticks[3] = {};
};
struct Pass {
    Cell by[kShaderCount];
    uint64_t draws = 0, sameMesh = 0, sameKey = 0, meshes = 0;
};
struct HookInfo { Fn orig = nullptr; int shader = 0; int kind = 0; };

HookInfo g_info[kHooks];
Fn g_accumOrig = nullptr;
bool g_installed = false;
DWORD g_renderTid = 0;
uint64_t g_tscPerMs = 0, g_frames = 0, g_gen = 1;
Pass g_pass[kPasses];
int g_cur = kPasses - 1, g_callIndex = 0, g_accumDepth = 0;
struct MeshSlot { const void* p; uint64_t gen; };
std::vector<MeshSlot> g_meshSet;   // kPasses * kMeshSet

// the running state of the current pass, reset at its start
struct Running { int shader = -1; uint64_t tech = ~0ull, mat = ~0ull; const void* mesh = nullptr; uint64_t key[3] = {}; };
Running g_run;

const void* MeshOf(uint64_t renderPass) {
    // BSRenderPass+0x10 is the BSGeometry; its renderer data (the mesh's buffers) sits at +0x138 on 1.6
    __try {
        const auto geo = *reinterpret_cast<const uint64_t*>(renderPass + 0x10);
        return geo ? *reinterpret_cast<void* const*>(geo + 0x138) : nullptr;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

void NoteMesh(int pass, const void* mesh) {
    if (!mesh) return;
    MeshSlot* set = g_meshSet.data() + size_t(pass) * kMeshSet;
    size_t i = size_t((reinterpret_cast<uintptr_t>(mesh) >> 4) * 0x9E3779B97F4A7C15ull >> 40) & (kMeshSet - 1);
    for (size_t n = 0; n < kMeshSet; ++n, i = (i + 1) & (kMeshSet - 1)) {
        if (set[i].gen != g_gen) { set[i] = {mesh, g_gen}; ++g_pass[pass].meshes; return; }
        if (set[i].p == mesh) return;
    }
}

template <int N>
uint64_t Hook(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f) {
    const HookInfo& h = g_info[N];
    if (GetCurrentThreadId() != g_renderTid) return h.orig(a, b, c, d, e, f);
    Pass& p = g_pass[g_cur];
    Cell& cell = p.by[h.shader];
    if (h.kind == kTech) {
        ++cell.tech;
        if (g_run.shader == h.shader && g_run.tech == b) ++cell.techRep;
        g_run.shader = h.shader; g_run.tech = b;
    } else if (h.kind == kMat) {
        ++cell.mat;
        if (g_run.mat == b) ++cell.matRep;
        g_run.mat = b;
    } else {
        ++cell.draws; ++p.draws;
        const void* mesh = MeshOf(b);
        if (mesh && mesh == g_run.mesh) ++p.sameMesh;
        const uint64_t key[3] = {uint64_t(h.shader) << 32 ^ g_run.tech, g_run.mat, uint64_t(mesh)};
        if (mesh && !std::memcmp(key, g_run.key, sizeof key)) ++p.sameKey;
        std::memcpy(g_run.key, key, sizeof key);
        g_run.mesh = mesh;
        NoteMesh(g_cur, mesh);
    }
    const uint64_t t0 = __rdtsc();
    const uint64_t r = h.orig(a, b, c, d, e, f);
    cell.ticks[h.kind] += __rdtsc() - t0;
    return r;
}

uint64_t AccumHook(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f) {
    if (GetCurrentThreadId() != g_renderTid || g_accumDepth++ != 0) {
        const uint64_t r = g_accumOrig(a, b, c, d, e, f);
        if (GetCurrentThreadId() == g_renderTid) --g_accumDepth;
        return r;
    }
    g_cur = g_callIndex < kPasses - 1 ? g_callIndex : kPasses - 1;
    ++g_callIndex;
    g_run = Running{};
    const uint64_t r = g_accumOrig(a, b, c, d, e, f);
    g_cur = kPasses - 1;
    g_run = Running{};
    --g_accumDepth;
    return r;
}

template <int... I>
constexpr std::array<void*, sizeof...(I)> Table(std::integer_sequence<int, I...>) {
    return {reinterpret_cast<void*>(&Hook<I>)...};
}
const auto kHookFns = Table(std::make_integer_sequence<int, kHooks>{});

bool Swap(const Image& img, uint32_t vtable, uint32_t slot, void* hook, Fn* orig) {
    if (!vtable) return false;   // not in this version's table
    auto* entry = reinterpret_cast<uint64_t*>(const_cast<uint8_t*>(img.base) + vtable) + slot;
    if (!img.InText(uint32_t(*entry - uint64_t(img.base)))) return false;
    *orig = reinterpret_cast<Fn>(*entry);
    DWORD old;
    if (!VirtualProtect(entry, 8, PAGE_READWRITE, &old)) return false;
    InterlockedExchange64(reinterpret_cast<volatile LONG64*>(entry), LONG64(hook));
    VirtualProtect(entry, 8, old, &old);
    return true;
}

void Calibrate() {
    LARGE_INTEGER f, q0, q1;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&q0);
    const uint64_t t0 = __rdtsc();
    Sleep(20);
    QueryPerformanceCounter(&q1);
    g_tscPerMs = uint64_t(double(__rdtsc() - t0) / (double(q1.QuadPart - q0.QuadPart) * 1000.0 / double(f.QuadPart)));
}

}  // namespace

std::string DrawStatsInstall(const Image& img) {
    if (g_installed) return "drawstats: already installed";
    Calibrate();
    g_meshSet.assign(size_t(kPasses) * kMeshSet, MeshSlot{nullptr, 0});
    g_renderTid = GetCurrentThreadId();   // installed from a present: the render thread
    int n = 0;
    for (int s = 0; s < kShaderCount; ++s)
        for (int k = 0; k < 3; ++k) {
            HookInfo& h = g_info[s * 3 + k];
            h.shader = s; h.kind = k;
            n += Swap(img, Addr(kShaders[s].vtable), kSlotOf[k], kHookFns[size_t(s * 3 + k)], &h.orig);
        }
    const bool accum = Swap(img, Addr("vtable:BSShaderAccumulator"), kAccumSlot, reinterpret_cast<void*>(&AccumHook), &g_accumOrig);
    g_installed = true;
    char b[128];
    std::snprintf(b, sizeof b, "drawstats: %d of %d shader slots hooked, accumulator %s", n, kHooks, accum ? "hooked" : "failed");
    return b;
}

void DrawStatsOnPresent() {
    if (!g_installed) return;
    ++g_frames; ++g_gen;
    g_callIndex = 0;
}

void DrawStatsReset() {
    for (auto& p : g_pass) p = Pass{};
    g_frames = 0;
}

std::string DrawStatsStats() {
    if (!g_installed || !g_frames) return "";
    const double fr = double(g_frames), ms = double(g_tscPerMs) * fr;
    std::string out = ",\"drawstats\":[";
    bool first = true;
    char b[512];
    for (int k = 0; k < kPasses; ++k) {
        const Pass& p = g_pass[k];
        if (!p.draws) continue;
        std::snprintf(b, sizeof b, "%s{\"pass\":%d,\"draws\":%.1f,\"meshes\":%.1f,\"same_mesh\":%.1f,\"same_key\":%.1f,\"by\":{",
                      first ? "" : ",", k == kPasses - 1 ? -1 : k, p.draws / fr, p.meshes / fr, p.sameMesh / fr, p.sameKey / fr);
        out += b;
        first = false;
        bool firstS = true;
        for (int s = 0; s < kShaderCount; ++s) {
            const Cell& c = p.by[s];
            if (!c.draws && !c.mat && !c.tech) continue;
            std::snprintf(b, sizeof b,
                          "%s\"%s\":{\"draws\":%.1f,\"tech\":%.1f,\"tech_rep\":%.1f,\"mat\":%.1f,\"mat_rep\":%.1f,"
                          "\"tech_ms\":%.4f,\"mat_ms\":%.4f,\"geo_ms\":%.4f}",
                          firstS ? "" : ",", kShaders[s].name, c.draws / fr, c.tech / fr, c.techRep / fr, c.mat / fr,
                          c.matRep / fr, c.ticks[0] / ms, c.ticks[1] / ms, c.ticks[2] / ms);
            out += b;
            firstS = false;
        }
        out += "}}";
    }
    return out + "]";
}

}  // namespace raw::rt
