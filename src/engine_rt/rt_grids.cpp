// Live loaded-grid size (RAW engine runtime). Original work, MIT. See rt_grids.h.
#include "rt_grids.h"
#include "rt_version.h"

#include <windows.h>

#include <algorithm>
#include <cstdio>

#include "rt_control.h"
#include "rt_detour.h"
#include "rt_peek.h"

namespace raw::rt {
namespace {

// TES* (TES::Singleton) and TES::gridCells, a GridCellArray*, from the version table
uint32_t g_capacity = 0;                         // the largest length seen: what the cell array holds
// Inline detours (they catch vtable and direct calls alike): GridArray::DetachAll 0x221380 (GridCellArray slot 1),
// GridCellArray::KillAll 0x221990 (slot 2), and 0x2216C0, the routine every grid builder calls with the new array
// (sub_19BF30 after reading and clamping uGridsToLoad, sub_1999E0, sub_1A2A00; engine map call graph).

const Image* g_img = nullptr;
const std::vector<RefTarget>* g_refs = nullptr;
uint32_t g_pending = 0;                          // a length waiting for the grid to empty
uint32_t g_applied = 0;                          // lengths applied from the hooks
uint32_t g_detachCalls = 0, g_killCalls = 0, g_builds = 0, g_lastBuildLength = 0;
using Fn = void (*)(void*);
using BuildFn = uint64_t (*)(uint64_t, uint64_t, uint64_t, uint64_t);
Fn g_detachAll = nullptr, g_killAll = nullptr;
BuildFn g_build = nullptr;
std::string g_hookResult;

struct Grid { uint8_t* arr = nullptr; int32_t x = 0, y = 0; uint32_t length = 0; uint64_t cells = 0; uint32_t loaded = 0; };

bool Read(const Image& img, Grid* g) {
    uint64_t tes = 0, arr = 0, xy = 0, len = 0;
    const uint32_t single = Addr("TES::Singleton"), cells = Off("off:TES::gridCells");
    if (!single || !cells) return false;
    if (!ReadU64(img.base + single, &tes) || !tes) return false;
    if (!ReadU64(reinterpret_cast<void*>(tes + cells), &arr) || !arr) return false;
    if (!ReadU64(reinterpret_cast<void*>(arr + 0x08), &xy) || !ReadU64(reinterpret_cast<void*>(arr + 0x10), &len)) return false;
    if (!ReadU64(reinterpret_cast<void*>(arr + 0x18), &g->cells)) return false;
    g->arr = reinterpret_cast<uint8_t*>(arr);
    g->x = int32_t(uint32_t(xy));
    g->y = int32_t(uint32_t(xy >> 32));
    g->length = uint32_t(len);
    if (g->length == 0 || g->length > 31 || !g->cells) return false;
    g->loaded = 0;
    for (uint32_t i = 0; i < g->length * g->length; ++i) {
        uint64_t c = 0;
        if (!ReadU64(reinterpret_cast<void*>(g->cells + i * 8ull), &c)) return false;
        g->loaded += c != 0;
    }
    g_capacity = std::max(g_capacity, g->length);
    return true;
}

bool Empty(const Grid& g) { return g.loaded == 0; }

// The new length lands while the array holds no cell; the next SetCenter fills the new grid.
std::string Apply(const Grid& g, uint32_t want) {
    const uint32_t before = g.length;
    *reinterpret_cast<volatile uint32_t*>(g.arr + 0x10) = want;
    std::string old;
    const bool ok = SetSetting(*g_img, *g_refs, "uGridsToLoad:General", std::to_string(want), &old);
    char extra[128];
    std::snprintf(extra, sizeof extra, ",\"before\":%u,\"setting_before\":\"%s\",\"setting_ok\":%s", before, old.c_str(),
                  ok ? "true" : "false");
    return extra;
}

// Growth past the array the game made: the game's own resize, TES::SetupGrid (fn:TES::SetupGrid; 0x1A1340 on
// 1.7.104). It writes uGridsToLoad, allocates a new GridCellArray through the game's heap (its constructor sizes the
// cells from the setting, odd and at least 5), centres it where the old one was, moves every cell both hold, and
// destroys the old array through its vtable. Called only while the array is empty, so no cell moves and no job
// thread walks a loaded grid while the setting and the array disagree.
using SetupGridFn = void (*)(void* tes, uint32_t length);
uint32_t g_grown = 0;
std::string Grow(uint32_t want) {
    const uint32_t fn = Addr("fn:TES::SetupGrid"), single = Addr("TES::Singleton");
    if (!fn || !single) return "no fn:TES::SetupGrid in this version's table";
    uint64_t tes = 0;
    if (!ReadU64(g_img->base + single, &tes) || !tes) return "no TES";
    reinterpret_cast<SetupGridFn>(const_cast<uint8_t*>(g_img->base) + fn)(reinterpret_cast<void*>(tes), want);
    ++g_grown;
    return "grown by TES::SetupGrid";
}

void AfterEmptying(void* arr) {
    if (!g_pending) return;
    Grid g;
    if (!Read(*g_img, &g) || g.arr != arr || !Empty(g)) return;
    if (g_pending > g_capacity) Grow(g_pending);
    else Apply(g, g_pending);
    g_pending = 0;
    ++g_applied;
}
void DetachAllHook(void* self) { ++g_detachCalls; g_detachAll(self); AfterEmptying(self); }
void KillAllHook(void* self) { ++g_killCalls; g_killAll(self); AfterEmptying(self); }
// The builder's arguments are not named by any source; the array it fills is read back after it returns.
uint64_t BuildHook(uint64_t a, uint64_t b, uint64_t c, uint64_t d) {
    const uint64_t r = g_build(a, b, c, d);
    ++g_builds;
    uint64_t len = 0;
    if (ReadU64(reinterpret_cast<void*>(a + 0x10), &len) && uint32_t(len) && uint32_t(len) < 32) g_lastBuildLength = uint32_t(len);
    return r;
}

std::string Hooks(const Image& img) {
    std::string out;
    out += "DetachAll " + InstallDetour(img, Addr("fn:GridArray::DetachAll"), reinterpret_cast<void*>(&DetachAllHook), reinterpret_cast<void**>(&g_detachAll));
    out += "; KillAll " + InstallDetour(img, Addr("fn:GridCellArray::KillAll"), reinterpret_cast<void*>(&KillAllHook), reinterpret_cast<void**>(&g_killAll));
    out += "; build " + InstallDetour(img, Addr("fn:GridCellArray::Build"), reinterpret_cast<void*>(&BuildHook), reinterpret_cast<void**>(&g_build));
    return out;
}

std::string Line(const char* what, const Grid& g, const char* extra) {
    char b[1024];
    std::snprintf(b, sizeof b,
                  "{\"cmd\":\"grids\",\"result\":\"%s\",\"world_x\":%d,\"world_y\":%d,\"length\":%u,\"capacity\":%u,"
                  "\"loaded_cells\":%u,\"detach_all_calls\":%u,\"kill_all_calls\":%u,\"builds\":%u,\"last_build_length\":%u,"
                  "\"grown\":%u,\"hooks\":\"%s\"%s}",
                  what, g.x, g.y, g.length, g_capacity, g.loaded, g_detachCalls, g_killCalls, g_builds, g_lastBuildLength, g_grown,
                  g_hookResult.c_str(), extra);
    return b;
}

}  // namespace

std::string GridsCommand(const Image& img, const std::vector<RefTarget>& refs, const std::string& arg) {
    if (!g_img) {
        g_img = &img; g_refs = &refs;
        g_hookResult = Hooks(img);
    }
    Grid g;
    if (!Read(img, &g)) return "{\"cmd\":\"grids\",\"error\":\"TES grid array not readable (no world loaded?)\"}";
    char extra[160];
    std::snprintf(extra, sizeof extra, ",\"pending\":%u,\"applied_by_hook\":%u", g_pending, g_applied);
    if (arg.empty() || arg == "show") return Line("show", g, extra);
    const uint32_t want = uint32_t(std::strtoul(arg.c_str(), nullptr, 10));
    // The array keeps the cell slots it was made with: a length past them would index outside it. Grid sizes
    // are odd so the player's cell stays in the middle.
    // Past the capacity the array is rebuilt by the game's own resize (Grow), which needs uExterior Cell Buffer to
    // hold the larger grid (set at startup, [EngineSet]); 31 is the reader's bound.
    if (want < 3 || want % 2 == 0 || want > 31) return Line("refused: odd, 3 to 31", g, extra);
    if (want > g_capacity && !Addr("fn:TES::SetupGrid")) return Line("refused: past the capacity and no fn:TES::SetupGrid", g, extra);
    if (!Empty(g)) {
        // Interiors keep the exterior grid loaded; the length waits for the game to empty the array
        // (DetachAll: a worldspace change). uGridsToLoad changes with the length, never before it: written early,
        // a job thread indexed the 5-wide array as 7 wide and faulted at SkyrimSE.exe+0x1A2E7E (ec7-1, 2026-11-19).
        g_pending = want;
        std::snprintf(extra, sizeof extra, ",\"pending\":%u,\"applied_by_hook\":%u", g_pending, g_applied);
        return Line("pending: applied when the game empties the grid", g, extra);
    }
    const std::string e = want > g_capacity ? ",\"grow\":\"" + Grow(want) + "\"" : Apply(g, want);
    Read(img, &g);
    return Line("set", g, e.c_str());
}

}  // namespace raw::rt
