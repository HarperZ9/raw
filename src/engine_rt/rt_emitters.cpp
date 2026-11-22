// Fire, torch and magic light: light names and glowing emitters (RAW engine runtime). Original work, MIT.
// See rt_emitters.h.
#include "rt_emitters.h"
#include "rt_patch.h"
#include "rt_version.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>

namespace raw::rt {
namespace {

using Fn = uint64_t (*)(uint64_t, uint64_t, uint64_t, uint64_t);
constexpr uint32_t kSetupGeometry = 6;
constexpr size_t kMaxRaw = 1024, kMaxOut = 128;
constexpr float kMerge = 48.0f;

struct Raw { const void* geo; float pos[3], radius, color[3]; uint32_t flags; };
Fn g_origEffect = nullptr, g_origParticle = nullptr, g_origLighting = nullptr;
DWORD g_renderTid = 0;
bool g_installed = false;
std::vector<Raw> g_raw;            // this frame's glowing draws (render thread only)
std::vector<Emitter> g_last;       // the last frame's merged list
uint64_t g_seen = 0, g_glowing = 0;
// why draws were not taken: no property or geometry, no alpha property, not blended, not additive, no material,
// too dim, a bound out of range, a read fault
uint64_t g_why[8] = {};
const char* kWhy[8] = {"no_property", "no_alpha", "not_blended", "not_additive", "no_material", "dim", "bound", "fault"};
std::mutex g_m;

bool Contains(const std::string& s, const char* const* words) {
    for (; *words; ++words)
        if (s.find(*words) != std::string::npos) return true;
    return false;
}

const char* const kFire[] = {"fire", "flame", "torch", "candle", "brazier", "lantern", "hearth", "ember", "sconce",
                             "chandelier", "lamp", "burn", "lava", "forge", "campfire", "bonfire", "magma",
                             "window", "skylight", "stained", nullptr};
const char* const kMagic[] = {"magic", "spell", "mgef", "fxmagic", "shock", "frost", "ward", "soul", "aura", "spark",
                              "heal", "restoration", "destruction", "conjuration", "illusion", "alteration",
                              "fireball", "firebolt", "lightning", "rune", "wisp", "glow", "dwemer", "nirn",
                              "plasma", "crystal", "mushroom", nullptr};

std::string Lower(const char* p, size_t max) {
    std::string s;
    for (size_t i = 0; i < max && p[i]; ++i) s += char(std::tolower(static_cast<unsigned char>(p[i])));
    return s;
}

// the draw's fields, read under structured exception handling (no objects that need unwinding here)
bool ReadDraw(uint64_t pass, Raw* r, char* path, size_t pathMax) {
    auto no = [](int k) { ++g_why[k]; return false; };
    __try {
        const auto* prop = *reinterpret_cast<const uint8_t* const*>(pass + 0x08);
        const auto* geo = *reinterpret_cast<const uint8_t* const*>(pass + 0x10);
        if (!prop || !geo) return no(0);
        const auto* alpha = *reinterpret_cast<const uint8_t* const*>(geo + 0x120);
        if (!alpha) return no(1);
        const uint16_t af = *reinterpret_cast<const uint16_t*>(alpha + 0x30);
        if (!(af & 1)) return no(2);
        // additive: destination factor ONE (0), or source ONE with destination ONE-MINUS-SOURCE-ALPHA (premultiplied glow)
        const uint32_t src = (af >> 1) & 0xF, dst = (af >> 5) & 0xF;
        if (dst != 0 && !(src == 0 && dst == 7)) return no(3);
        const auto* mat = *reinterpret_cast<const uint8_t* const*>(prop + 0x78);
        if (!mat) return no(4);
        float base[4], scale;
        std::memcpy(base, mat + 0x48, 16);
        std::memcpy(&scale, mat + 0x6C, 4);
        if (!(scale > 0.0f && scale < 100.0f)) scale = 1.0f;
        r->geo = geo;
        std::memcpy(r->pos, geo + 0xE4, 12);
        std::memcpy(&r->radius, geo + 0xF0, 4);
        const char* p = *reinterpret_cast<const char* const*>(mat + 0x70);
        // Replace Skyrim's static 2011 2D mountain-fog, cloud, and giant valley-mist billboard cards with RAW's
        // world-locked 160x90x64 3D Froxel Volumetric Participating Media by cleanly zeroing their material alpha.
        if (r->radius >= 260.0f && p) {
            char mBuf[96] = {};
            for (size_t i = 0; i + 1 < sizeof mBuf && p[i]; ++i) {
                const char c = p[i];
                mBuf[i] = (c >= 'A' && c <= 'Z') ? char(c + 32) : c;
            }
            if (std::strstr(mBuf, "mountainfog") || std::strstr(mBuf, "fxclouds") ||
                std::strstr(mBuf, "fxmist") || std::strstr(mBuf, "fxfog") || std::strstr(mBuf, "fxlowfog")) {
                *reinterpret_cast<float*>(const_cast<uint8_t*>(mat) + 0x54) = 0.0f;
            }
        }
        // over 400 units the glow is a mist or spray plane, not a source (ec7-4: radii of 480 to 1,540 were mist
        // over water and a whole-room effect in the inn)
        if (!(r->radius > 0.5f && r->radius < 400.0f)) return no(6);
        const float a = std::clamp(base[3], 0.0f, 1.0f);
        for (int i = 0; i < 3; ++i) r->color[i] = std::clamp(base[i], 0.0f, 16.0f) * scale * a;
        if (r->color[0] + r->color[1] + r->color[2] < 0.02f) return no(5);
        for (size_t i = 0; p && i + 1 < pathMax && p[i]; ++i) path[i] = p[i];
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ++g_why[7];
        return false;
    }
}

void Note(uint64_t pass) {
    ++g_seen;
    if (g_raw.size() >= kMaxRaw) return;
    Raw r{};
    char buf[96] = {};
    if (!ReadDraw(pass, &r, buf, sizeof buf)) return;
    const std::string path = Lower(buf, sizeof buf);
    // fire wins over magic: ember and spark textures belong to fires
    const bool fire = Contains(path, kFire);
    r.flags = kLightEmitter | (fire ? kLightFire : 0u) | (!fire && Contains(path, kMagic) ? kLightMagic : 0u);
    // a texture named for neither: warm colours read as fire
    if (!(r.flags & (kLightFire | kLightMagic)) && r.color[0] > 1.3f * r.color[2] && r.color[0] >= r.color[1]) r.flags |= kLightFire;
    ++g_glowing;
    g_raw.push_back(r);
}

const char* const kWindow[] = {"window", "skylight", "stainedglass", "glassglow", nullptr};

bool ReadWindowLight(uint64_t pass, Raw* r) {
    __try {
        const auto* geo = *reinterpret_cast<const uint8_t* const*>(pass + 0x10);
        if (!geo) return false;
        const char* name = *reinterpret_cast<const char* const*>(geo + 0x10);
        if (!name || !name[0]) return false;
        char buf[96] = {};
        for (size_t i = 0; i + 1 < sizeof buf && name[i]; ++i) {
            const char c = name[i];
            buf[i] = (c >= 'A' && c <= 'Z') ? char(c + 32) : c;
        }
        bool isWin = false;
        for (const char* const* w = kWindow; *w; ++w) {
            if (std::strstr(buf, *w)) { isWin = true; break; }
        }
        if (!isWin) return false;
        r->geo = geo;
        std::memcpy(r->pos, geo + 0xE4, 12);
        std::memcpy(&r->radius, geo + 0xF0, 4);
        if (!(r->radius >= 12.0f && r->radius <= 360.0f)) return false;
        // True 3D world-space window portal light (daylight-warm ivory default; modulated by exterior sun in Lighting.hlsl).
        r->color[0] = 1.45f; r->color[1] = 1.38f; r->color[2] = 1.28f;
        r->flags = kLightEmitter;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

uint64_t EffectHook(uint64_t a, uint64_t b, uint64_t c, uint64_t d) {
    if (GetCurrentThreadId() == g_renderTid) Note(b);
    return g_origEffect(a, b, c, d);
}
uint64_t ParticleHook(uint64_t a, uint64_t b, uint64_t c, uint64_t d) {
    if (GetCurrentThreadId() == g_renderTid) Note(b);
    return g_origParticle(a, b, c, d);
}
using SetBoundFn = void (*)(float, float, float, float);
SetBoundFn g_setBound = nullptr;

bool ReadGeoBound(uint64_t pass, float out[4]) {
    __try {
        const auto* geo = *reinterpret_cast<const uint8_t* const*>(pass + 0x10);
        if (!geo) return false;
        std::memcpy(out, geo + 0xE4, 16);  // exact NiAVObject::worldBound (center.xyz, radius)
        return std::isfinite(out[0]) && std::isfinite(out[1]) && std::isfinite(out[2]) &&
               std::isfinite(out[3]) && out[3] > 0.1f && out[3] < 100000.0f;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

uint64_t LightingHook(uint64_t a, uint64_t b, uint64_t c, uint64_t d) {
    if (GetCurrentThreadId() == g_renderTid) {
        if (g_setBound) {
            float bound[4];
            if (ReadGeoBound(b, bound)) g_setBound(bound[0], bound[1], bound[2], bound[3]);
        }
        if (g_raw.size() < kMaxRaw) {
            Raw r{};
            if (ReadWindowLight(b, &r)) { ++g_glowing; g_raw.push_back(r); }
        }
    }
    return g_origLighting(a, b, c, d);
}

bool Swap(const Image& img, uint32_t vtable, void* hook, Fn* orig) {
    if (!vtable) return false;   // not in this version's table
    auto* entry = reinterpret_cast<uint64_t*>(const_cast<uint8_t*>(img.base) + vtable) + kSetupGeometry;
    if (!img.InText(uint32_t(*entry - uint64_t(img.base)))) return false;
    *orig = reinterpret_cast<Fn>(*entry);
    return PatchPointer(entry, hook);   // rt_patch.h: a module stop puts the slot back
}

// one geometry counts once a frame (it can draw in several passes); nearby draws merge into one light
std::vector<Emitter> Merge(std::vector<Raw>& raw) {
    std::sort(raw.begin(), raw.end(), [](const Raw& x, const Raw& y) { return x.geo < y.geo; });
    raw.erase(std::unique(raw.begin(), raw.end(), [](const Raw& x, const Raw& y) { return x.geo == y.geo; }), raw.end());
    std::vector<Emitter> out;
    for (const Raw& r : raw) {
        Emitter* hit = nullptr;
        for (auto& e : out) {
            const float dx = e.pos[0] - r.pos[0], dy = e.pos[1] - r.pos[1], dz = e.pos[2] - r.pos[2];
            if (dx * dx + dy * dy + dz * dz < kMerge * kMerge) { hit = &e; break; }
        }
        if (!hit) {
            if (out.size() >= kMaxOut) continue;
            Emitter e{};
            std::memcpy(e.pos, r.pos, 12);
            e.flags = r.flags;
            out.push_back(e);
            hit = &out.back();
        }
        const float w = float(hit->draws);
        for (int i = 0; i < 3; ++i) { hit->pos[i] = (hit->pos[i] * w + r.pos[i]) / (w + 1); hit->color[i] += r.color[i]; }
        hit->radius = std::max(hit->radius, r.radius);
        hit->flags |= r.flags;
        ++hit->draws;
    }
    return out;
}

}  // namespace

uint32_t LightFlagsFromNames(const std::string& names) {
    return (Contains(names, kFire) ? kLightFire : 0u) | (Contains(names, kMagic) && !Contains(names, kFire) ? kLightMagic : 0u);
}

std::string EmittersInstall(const Image& img) {
    if (g_installed) return "emitters: already installed";
    g_renderTid = GetCurrentThreadId();
    g_raw.reserve(kMaxRaw);
    if (HMODULE proxy = GetModuleHandleA("d3d11.dll"))
        g_setBound = reinterpret_cast<SetBoundFn>(GetProcAddress(proxy, "PG_SetCurrentDrawBound"));
    const bool a = Swap(img, Addr("vtable:BSEffectShader"), reinterpret_cast<void*>(&EffectHook), &g_origEffect);
    const bool b = Swap(img, Addr("vtable:BSParticleShader"), reinterpret_cast<void*>(&ParticleHook), &g_origParticle);
    const bool c = Swap(img, Addr("vtable:BSLightingShader"), reinterpret_cast<void*>(&LightingHook), &g_origLighting);
    g_installed = a || b || c;
    return std::string("emitters: effect ") + (a ? "hooked" : "failed") + ", particle " + (b ? "hooked" : "failed") + ", lighting-windows " + (c ? "hooked" : "failed");
}

void EmittersOnPresent() {
    if (!g_installed) return;
    // the thread that presents is the one that draws; at the first present (the main menu) it was another thread
    // than in game (ec7-3: 2,088 draws counted at the menu, none after), so it is taken again each present
    g_renderTid = GetCurrentThreadId();
    std::vector<Emitter> merged = Merge(g_raw);
    g_raw.clear();
    std::lock_guard<std::mutex> g(g_m);
    // Temporal EMA flicker smoothing for particle/spell/flame emitters: prevents harsh 1-frame particle-count strobing
    // while keeping moving torches, spells, and projectiles responsive (55% pos, 35% intensity per frame).
    for (auto& cur : merged) {
        const Emitter* bestPrev = nullptr;
        float bestD2 = 56.0f * 56.0f;
        for (const auto& prev : g_last) {
            const float dx = cur.pos[0] - prev.pos[0], dy = cur.pos[1] - prev.pos[1], dz = cur.pos[2] - prev.pos[2];
            const float d2 = dx * dx + dy * dy + dz * dz;
            if (d2 < bestD2) { bestD2 = d2; bestPrev = &prev; }
        }
        if (bestPrev) {
            for (int i = 0; i < 3; ++i) {
                cur.pos[i] = bestPrev->pos[i] + (cur.pos[i] - bestPrev->pos[i]) * 0.55f;
                cur.color[i] = bestPrev->color[i] + (cur.color[i] - bestPrev->color[i]) * 0.35f;
            }
            cur.radius = bestPrev->radius + (cur.radius - bestPrev->radius) * 0.40f;
        }
    }
    g_last = std::move(merged);
}

std::vector<Emitter> EmittersLast() {
    std::lock_guard<std::mutex> g(g_m);
    return g_last;
}

std::string EmittersJson() {
    const auto list = EmittersLast();
    char b[256];
    std::snprintf(b, sizeof b, "{\"cmd\":\"emitters\",\"installed\":%s,\"draws_seen\":%llu,\"glowing\":%llu,\"emitters\":%zu,\"list\":[",
                  g_installed ? "true" : "false", (unsigned long long)g_seen, (unsigned long long)g_glowing, list.size());
    std::string out = b;
    out.insert(out.size() - 9, ",\"skipped\":{");
    for (int k = 0; k < 8; ++k) {
        std::snprintf(b, sizeof b, "%s\"%s\":%llu", k ? "," : "", kWhy[k], (unsigned long long)g_why[k]);
        out.insert(out.size() - 9, b);
    }
    out.insert(out.size() - 9, "}");
    for (size_t i = 0; i < list.size() && i < 48; ++i) {
        const Emitter& e = list[i];
        std::snprintf(b, sizeof b, "%s{\"pos\":[%.1f,%.1f,%.1f],\"radius\":%.1f,\"color\":[%.3f,%.3f,%.3f],\"flags\":%u,\"draws\":%u}",
                      i ? "," : "", e.pos[0], e.pos[1], e.pos[2], e.radius, e.color[0], e.color[1], e.color[2], e.flags, e.draws);
        out += b;
    }
    return out + "]}";
}

}  // namespace raw::rt
