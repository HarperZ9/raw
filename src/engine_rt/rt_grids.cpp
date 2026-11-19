// Live loaded-grid size (RAW engine runtime). Original work, MIT. See rt_grids.h.
#include "rt_grids.h"

#include <windows.h>

#include <algorithm>
#include <cstdio>

#include "rt_control.h"
#include "rt_peek.h"

namespace raw::rt {
namespace {

constexpr uint32_t kTesSingleton = 0x31872C8;   // TES* (engine map, singletons.tsv)
constexpr uint32_t kGridOffset = 0x78;           // TES::gridCells, a GridCellArray*
uint32_t g_capacity = 0;                         // the largest length seen: what the cell array holds
constexpr uint32_t kGridVtable = 0x178B160;      // GridCellArray::vftable; slot 1 DetachAll, slot 2 KillAll
const Image* g_img = nullptr;
const std::vector<RefTarget>* g_refs = nullptr;
uint32_t g_pending = 0;                          // a length waiting for the grid to empty
uint32_t g_applied = 0;                          // lengths applied from the hooks
using Fn = void (*)(void*);
Fn g_detachAll = nullptr, g_killAll = nullptr;

struct Grid { uint8_t* arr = nullptr; int32_t x = 0, y = 0; uint32_t length = 0; uint64_t cells = 0; uint32_t loaded = 0; };

bool Read(const Image& img, Grid* g) {
    uint64_t tes = 0, arr = 0, xy = 0, len = 0;
    if (!ReadU64(img.base + kTesSingleton, &tes) || !tes) return false;
    if (!ReadU64(reinterpret_cast<void*>(tes + kGridOffset), &arr) || !arr) return false;
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

void AfterEmptying(void* arr) {
    if (!g_pending) return;
    Grid g;
    if (!Read(*g_img, &g) || g.arr != arr || !Empty(g)) return;
    Apply(g, g_pending);
    g_pending = 0;
    ++g_applied;
}
void DetachAllHook(void* self) { g_detachAll(self); AfterEmptying(self); }
void KillAllHook(void* self) { g_killAll(self); AfterEmptying(self); }

bool Swap(const Image& img, uint32_t slot, void* hook, Fn* orig) {
    auto* entry = reinterpret_cast<uint64_t*>(const_cast<uint8_t*>(img.base) + kGridVtable) + slot;
    if (!img.InText(uint32_t(*entry - uint64_t(img.base)))) return false;
    *orig = reinterpret_cast<Fn>(*entry);
    DWORD old;
    if (!VirtualProtect(entry, 8, PAGE_READWRITE, &old)) return false;
    InterlockedExchange64(reinterpret_cast<volatile LONG64*>(entry), LONG64(hook));
    VirtualProtect(entry, 8, old, &old);
    return true;
}

std::string Line(const char* what, const Grid& g, const char* extra) {
    char b[320];
    std::snprintf(b, sizeof b,
                  "{\"cmd\":\"grids\",\"result\":\"%s\",\"world_x\":%d,\"world_y\":%d,\"length\":%u,\"capacity\":%u,"
                  "\"loaded_cells\":%u%s}",
                  what, g.x, g.y, g.length, g_capacity, g.loaded, extra);
    return b;
}

}  // namespace

std::string GridsCommand(const Image& img, const std::vector<RefTarget>& refs, const std::string& arg) {
    if (!g_img) {
        g_img = &img; g_refs = &refs;
        const bool a = Swap(img, 1, reinterpret_cast<void*>(&DetachAllHook), &g_detachAll);
        const bool b = Swap(img, 2, reinterpret_cast<void*>(&KillAllHook), &g_killAll);
        if (!a || !b) return "{\"cmd\":\"grids\",\"error\":\"GridCellArray DetachAll/KillAll hooks failed\"}";
    }
    Grid g;
    if (!Read(img, &g)) return "{\"cmd\":\"grids\",\"error\":\"TES grid array not readable (no world loaded?)\"}";
    char extra[160];
    std::snprintf(extra, sizeof extra, ",\"pending\":%u,\"applied_by_hook\":%u", g_pending, g_applied);
    if (arg.empty() || arg == "show") return Line("show", g, extra);
    const uint32_t want = uint32_t(std::strtoul(arg.c_str(), nullptr, 10));
    // The array keeps the cell slots it was made with: a length past them would index outside it. Grid sizes
    // are odd so the player's cell stays in the middle.
    if (want < 3 || want % 2 == 0 || want > g_capacity) return Line("refused: odd, 3 or more, up to the capacity", g, extra);
    if (!Empty(g)) {
        // Interiors keep the exterior grid loaded; the length waits for the game to empty the array
        // (DetachAll or KillAll: a worldspace change or a load).
        g_pending = want;
        std::snprintf(extra, sizeof extra, ",\"pending\":%u,\"applied_by_hook\":%u", g_pending, g_applied);
        return Line("pending: applied when the game empties the grid", g, extra);
    }
    const std::string e = Apply(g, want);
    Read(img, &g);
    return Line("set", g, e.c_str());
}

}  // namespace raw::rt
