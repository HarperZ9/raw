// RAW drives the shadow cascades per scene (RAW engine runtime). Original work, MIT. See rt_cascades.h.
#include "rt_cascades.h"

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <sstream>

#include "rt_control.h"
#include "rt_view.h"

namespace raw::rt {
namespace {

struct Preset { float dist = 0, first = 0, overlap = 0; uint32_t atlas = 0; bool set = false; };
enum Scene : int { kExterior = 0, kInterior = 1, kVista = 2, kNone = 3 };
const char* kSceneName[] = {"exterior", "interior", "vista", "none"};

const Image* g_img = nullptr;
const std::vector<RefTarget>* g_refs = nullptr;
Preset g_preset[3];
bool g_auto = false;
float g_vistaZ = 9000.0f;
int g_scene = kNone;
uint32_t g_changes = 0;

float Get(const char* name) {
    for (const auto& r : *g_refs)
        if (r.setting && r.name == name) { float v; std::memcpy(&v, g_img->base + r.rva + 8, 4); return v; }
    return -1.0f;
}
void Put(const char* name, float v) { SetSetting(*g_img, *g_refs, name, std::to_string(v)); }

Preset Parse(const char* s) {
    Preset p;
    p.set = std::sscanf(s, "%f,%f,%f,%u", &p.dist, &p.first, &p.overlap, &p.atlas) >= 3 && p.dist > 0;
    return p;
}

std::string Current(const char* what) {
    char b[384];
    std::snprintf(b, sizeof b,
                  "{\"cmd\":\"cascades\",\"result\":\"%s\",\"scene\":\"%s\",\"auto\":%s,\"distance\":%.0f,\"first_slice\":%.0f,"
                  "\"overlap\":%.3f,\"interior_distance\":%.0f,\"changes\":%u}",
                  what, kSceneName[g_scene], g_auto ? "true" : "false", Get("fShadowDistance:Display"),
                  Get("fFirstSliceDistance:Display"), Get("fSplitOverlap:Display"), Get("fInteriorShadowDistance:Display"),
                  g_changes);
    return b;
}

int SceneNow() {
    EngineView v{};
    RAW_EngineView(&v);
    if (!(v.ok & 4)) return kNone;
    if (v.interior) return kInterior;
    // a height, not a height above the player: with tcl the player rises with the camera (ec7-1)
    if ((v.ok & 2) && v.camPos[2] > g_vistaZ) return kVista;
    return kExterior;
}

}  // namespace

void CascadesInit(const Image& img, const std::vector<RefTarget>* refs, const std::string& ini) {
    g_img = &img; g_refs = refs;
    const char* def[3] = {"", "", ""};
    const char* keys[3] = {"Exterior", "Interior", "Vista"};
    for (int i = 0; i < 3; ++i) {
        char b[128];
        GetPrivateProfileStringA("Cascades", keys[i], def[i], b, sizeof b, ini.c_str());
        g_preset[i] = Parse(b);
    }
    g_auto = GetPrivateProfileIntA("Cascades", "Auto", 0, ini.c_str()) != 0;
    g_vistaZ = float(GetPrivateProfileIntA("Cascades", "VistaZ", 9000, ini.c_str()));
}

uint32_t CascadesOnPresent(std::string* line) {
    if (!g_img || !g_auto) return 0;
    const int s = SceneNow();
    if (s == g_scene || s == kNone) return 0;
    g_scene = s;
    const Preset& p = g_preset[s];
    if (!p.set) { if (line) *line = Current("scene changed, no preset"); return 0; }
    if (s == kInterior) Put("fInteriorShadowDistance:Display", p.dist);
    else Put("fShadowDistance:Display", p.dist);
    Put("fFirstSliceDistance:Display", p.first);
    Put("fSplitOverlap:Display", p.overlap);
    ++g_changes;
    if (line) *line = Current("preset applied");
    return p.atlas;
}

std::string CascadesCommand(const std::string& op, const std::string& rest) {
    if (!g_img) return "{\"cmd\":\"cascades\",\"error\":\"not initialised\"}";
    std::istringstream in(rest);
    if (op == "set") {
        float d = 0, f = 0, o = -1, i = -1;
        in >> d >> f >> o >> i;
        if (d > 0) Put("fShadowDistance:Display", d);
        if (f > 0) Put("fFirstSliceDistance:Display", f);
        if (o >= 0) Put("fSplitOverlap:Display", o);
        if (i > 0) Put("fInteriorShadowDistance:Display", i);
        ++g_changes;
        return Current("set");
    }
    if (op == "auto") {
        int on = 0;
        in >> on;
        g_auto = on != 0;
        g_scene = kNone;   // the next present applies the scene's preset
        return Current(g_auto ? "auto on" : "auto off");
    }
    if (op == "preset") {
        // cascades preset <exterior|interior|vista> dist,first,overlap,atlas
        std::string which, spec;
        in >> which >> spec;
        for (int k = 0; k < 3; ++k)
            if (which == kSceneName[k]) { g_preset[k] = Parse(spec.c_str()); g_scene = kNone; return Current("preset stored"); }
        return "{\"cmd\":\"cascades\",\"error\":\"preset: exterior, interior or vista\"}";
    }
    return Current("show");
}

}  // namespace raw::rt
