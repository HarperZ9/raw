// Live engine state (RAW engine runtime). Original work, MIT. See rt_livestate.h.
#include "rt_livestate.h"
#include "rt_version.h"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <vector>

#include "rt_emitters.h"
#include "rt_view.h"

namespace raw::rt {
namespace {


constexpr uint32_t kMaxLights = 512;

const uint8_t* g_base = nullptr;
uint32_t kShaderState = 0, kGraphicsState = 0;   // from the version table at LiveStateInit
std::string g_out;
uint64_t g_frame = 0;
uint32_t g_every = 0;
bool g_exportEmitters = false;   // emitter lights join RAW_EngineLights only when on ([EngineRT] EmitterLights, `emitters export`)

struct Snapshot {
    uint64_t frame = 0;
    bool ok = false, interior = false, insideFrame = false;
    float timers[5] = {}, grid[2] = {}, ambientSpec[4] = {}, charLight[4] = {}, waterIntersect = 0;
    uint32_t technique = 0, inWater = 0, active = 0, casters = 0;
    std::vector<EngineLight> lights;
    std::vector<std::string> names;   // per light: its own name and its parents', lower case
    uint32_t emitters = 0;
};
Snapshot g_snap;
std::mutex g_m;   // guards g_snap for RAW_EngineLights callers on other threads

template <class T> T At(const uint8_t* p, uint32_t off) { T v; std::memcpy(&v, p + off, sizeof v); return v; }

// a BSFixedString's characters (NiObjectNET +0x10), lower case, printable only
void AppendName(const uint8_t* obj, std::string* out) {
    const char* p = At<const char*>(obj, 0x10);
    if (!p) return;
    if (!out->empty()) *out += '/';
    for (int i = 0; i < 40 && p[i]; ++i) {
        const char c = p[i];
        if (c < 32 || c > 126) return;
        *out += char(c >= 'A' && c <= 'Z' ? c + 32 : c);
    }
}

// the light's name and up to three parents' (NiAVObject parent +0x30)
std::string Names(const uint8_t* ni) {
    std::string s;
    for (int d = 0; ni && d < 4; ++d) {
        AppendName(ni, &s);
        ni = At<const uint8_t*>(ni, 0x30);
    }
    return s;
}

// What the light belongs to: the first node up the chain with a reference (NiAVObject userData +0xF8). A light
// form (base +0x40, form type +0x1A == 31) that flickers or pulses (OBJ_LIGH flags at +0xEC: flicker 8, slow 64,
// pulse 128, slow pulse 256) or can be carried (2) is fire or a torch; a light under an actor (ACHR, 62) that is
// not a light form is a magic or effect light. Placed lights' nodes are all named "ptlight" (ec7-2), so names
// alone did not tell them apart.
uint32_t OwnerFlags(const uint8_t* ni) {
    for (int d = 0; ni && d < 6; ++d) {
        const uint8_t* ref = At<const uint8_t*>(ni, 0xF8);
        if (ref) {
            const uint8_t refType = ref[0x1A];
            const uint8_t* base = At<const uint8_t*>(ref, 0x40);
            if (base && base[0x1A] == 31) {
                const uint32_t lf = At<uint32_t>(base, 0xEC);
                return (lf & (2u | 8u | 64u | 128u | 256u)) ? kLightFire : 0u;
            }
            return refType == 62 ? kLightMagic : 0u;
        }
        ni = At<const uint8_t*>(ni, 0x30);
    }
    return 0;
}

bool ReadLight(const uint8_t* bsl, uint32_t flags, EngineLight* L, std::string* names = nullptr) {
    if (!bsl) return false;
    const uint8_t* ni = At<const uint8_t*>(bsl, 0x48);
    if (!ni) return false;
    // the NiLight's world translation (NiAVObject world transform +0x7C, translation +0x24); BSLight +0x50 read zero
    std::memcpy(L->pos, ni + 0xA0, 12);
    const float fade = At<float>(ni, 0x134);
    float diffuse[3];
    std::memcpy(diffuse, ni + 0x11C, 12);
    for (int i = 0; i < 3; ++i) L->color[i] = diffuse[i] * fade;
    L->radius = At<float>(ni, 0x128);
    L->fade = fade;
    L->flags = flags | (bsl[0x44] ? 1u : 0u);
    L->pad = 0;
    if (names && !(flags & 4)) {
        *names = Names(ni);
        L->flags |= LightFlagsFromNames(*names) | OwnerFlags(ni);
    }
    return true;
}

// lights of one BSTArray of BSLight pointers (NiPointer holds the raw pointer)
uint32_t ReadArray(const uint8_t* arr, uint32_t flags, Snapshot* s) {
    const auto* data = At<const uint8_t* const*>(arr, 0x00);
    const uint32_t size = At<uint32_t>(arr, 0x10);
    if (!data || size > 4096) return 0;
    for (uint32_t i = 0; i < size && s->lights.size() < kMaxLights; ++i) {
        EngineLight L{};
        std::string n;
        if (ReadLight(data[i], flags, &L, &n)) { s->lights.push_back(L); s->names.push_back(std::move(n)); }
    }
    return size;
}

// outside Read: its structured exception handler cannot hold objects that need unwinding
__declspec(noinline) void Push(Snapshot* s, const EngineLight& L, const uint8_t* bslForName, const char* fixed) {
    s->lights.push_back(L);
    s->names.push_back(fixed ? std::string(fixed) : Names(At<const uint8_t*>(bslForName, 0x48)));
}

// glowing effect and particle draws (rt_emitters.h) join as point lights flagged kLightEmitter
void AddEmitters(Snapshot* s) {
    for (const Emitter& e : EmittersLast()) {
        if (s->lights.size() >= kMaxLights) return;
        EngineLight L{};
        std::memcpy(L.pos, e.pos, 12);
        // an estimate, not an authored light: four times the glow's size, its summed colour at a quarter
        L.radius = std::clamp(e.radius * 4.0f, 64.0f, 512.0f);
        for (int i = 0; i < 3; ++i) L.color[i] = std::min(e.color[i] * 0.25f, 4.0f);
        L.fade = 1.0f;
        L.flags = 1u | e.flags;
        s->lights.push_back(L);
        s->names.push_back("emitter");
        ++s->emitters;
    }
}

bool Read(Snapshot* s) {
    __try {
        if (!kShaderState || !kGraphicsState) return false;
        const uint8_t* st = g_base + kShaderState;
        std::memcpy(s->timers, st + 0x20, sizeof s->timers);
        s->interior = st[0x44] != 0;
        std::memcpy(s->grid, st + 0xB0, 8);
        s->technique = At<uint32_t>(st, 0xBC);
        std::memcpy(s->ambientSpec, st + 0xFC, 16);
        std::memcpy(s->charLight, st + 0x10C, 16);
        s->inWater = At<uint32_t>(st, 0x128);
        s->waterIntersect = At<float>(st, 0x144);
        s->insideFrame = (g_base + kGraphicsState)[0x50] != 0;
        s->lights.clear();
        s->names.clear();
        const uint8_t* ssn = At<const uint8_t*>(st, 0x00);
        if (ssn) {
            EngineLight sun{};
            if (ReadLight(At<const uint8_t*>(ssn, 0x200), 4, &sun)) Push(s, sun, nullptr, "sun");
            s->active = ReadArray(ssn + 0x130, 0, s);
            // shadow casters are also in the active list: mark them there, add any that are not
            const auto* data = At<const uint8_t* const*>(ssn + 0x230, 0x00);
            const uint32_t n = std::min<uint32_t>(At<uint32_t>(ssn + 0x230, 0x10), 64);
            s->casters = data ? n : 0;
            for (uint32_t i = 0; data && i < n; ++i) {
                EngineLight L{};
                if (!ReadLight(data[i], 2, &L)) continue;
                bool found = false;
                for (auto& x : s->lights)
                    if (!std::memcmp(x.pos, L.pos, 12) && !(x.flags & 4)) { x.flags |= 2; found = true; break; }
                if (!found && s->lights.size() < kMaxLights) Push(s, L, data[i], nullptr);
            }
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

std::string Json(const Snapshot& s, bool withLights) {
    char b[1024];
    std::snprintf(b, sizeof b,
                  "{\"cmd\":\"live\",\"frame\":%llu,\"ok\":%s,\"interior\":%s,\"technique\":\"0x%X\",\"grid_center\":[%.0f,%.0f],"
                  "\"timers\":[%.3f,%.3f,%.3f,%.3f,%.3f],\"ambient_specular\":[%.3f,%.3f,%.3f,%.3f],"
                  "\"character_light\":[%.3f,%.3f,%.3f,%.3f],\"camera_in_water\":%u,\"water_intersect\":%.3f,"
                  "\"inside_frame\":%s,\"active_lights\":%u,\"shadow_casters\":%u,"
                  "\"emitter_lights\":%u,\"lights_out\":%zu",
                  (unsigned long long)s.frame, s.ok ? "true" : "false", s.interior ? "true" : "false", s.technique, s.grid[0],
                  s.grid[1], s.timers[0], s.timers[1], s.timers[2], s.timers[3], s.timers[4], s.ambientSpec[0],
                  s.ambientSpec[1], s.ambientSpec[2], s.ambientSpec[3], s.charLight[0], s.charLight[1], s.charLight[2],
                  s.charLight[3], s.inWater, s.waterIntersect,
                  s.insideFrame ? "true" : "false", s.active, s.casters, s.emitters, s.lights.size());
    std::string out = b;
    out += "," + ViewJson();
    if (withLights) {
        out += ",\"lights\":[";
        for (size_t i = 0; i < s.lights.size() && i < 64; ++i) {
            const auto& L = s.lights[i];
            std::snprintf(b, sizeof b, "%s{\"pos\":[%.1f,%.1f,%.1f],\"radius\":%.1f,\"color\":[%.3f,%.3f,%.3f],\"flags\":%u,\"name\":\"%s\"}",
                          i ? "," : "", L.pos[0], L.pos[1], L.pos[2], L.radius, L.color[0], L.color[1], L.color[2], L.flags,
                          i < s.names.size() ? s.names[i].c_str() : "");
            out += b;
        }
        out += "]";
    }
    return out + "}";
}

void Append(const std::string& line) {
    FILE* f = std::fopen((g_out + "\\live_state.jsonl").c_str(), "a");
    if (f) { std::fprintf(f, "%s\n", line.c_str()); std::fclose(f); }
}

}  // namespace

void LiveStateInit(const Image& img, const std::string& outDir, bool exportEmitters) {
    g_base = img.base; g_out = outDir; g_exportEmitters = exportEmitters;
    kShaderState = Addr("BSShaderManager::State::GetSingleton"); kGraphicsState = Addr("BSGraphics::State::GetSingleton");
}

void LiveStateExportEmitters(bool on) { g_exportEmitters = on; }

void LiveStateOnPresent() {
    if (!g_base) return;
    ++g_frame;
    Snapshot s;
    s.lights.reserve(64);
    ViewOnPresent();
    EmittersOnPresent();
    s.ok = Read(&s);
    if (s.ok && g_exportEmitters) AddEmitters(&s);
    s.frame = g_frame;
    if (g_every && g_frame % g_every == 0) Append(Json(s, false));
    std::lock_guard<std::mutex> g(g_m);
    g_snap = std::move(s);
}

std::string LiveStateCommand(const std::string& op, const std::string& arg) {
    if (op == "live") {
        g_every = uint32_t(std::strtoul(arg.c_str(), nullptr, 10));
        return "{\"cmd\":\"live\",\"every\":" + std::to_string(g_every) + "}";
    }
    std::lock_guard<std::mutex> g(g_m);
    return Json(g_snap, true);
}

}  // namespace raw::rt

extern "C" __declspec(dllexport) uint32_t RAW_EngineLights(raw::rt::EngineLight* out, uint32_t max, uint64_t* frame) {
    std::lock_guard<std::mutex> g(raw::rt::g_m);
    const auto& s = raw::rt::g_snap;
    const uint32_t n = uint32_t(s.lights.size());
    if (out) std::memcpy(out, s.lights.data(), sizeof(raw::rt::EngineLight) * std::min(n, max));
    if (frame) *frame = s.frame;
    return n;
}
