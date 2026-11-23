// The view: camera, cell, time and the screen (RAW engine runtime). Original work, MIT. See rt_view.h.
#include "rt_view.h"
#include "rt_version.h"

#include <windows.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>

namespace raw::rt {
namespace {



const uint8_t* g_base = nullptr;
// from the version table at ViewInit; a part whose address is 0 reads as not available
uint32_t kGraphicsState = 0, kPlayerCamera = 0, kPlayer = 0, kGetSky = 0, kCalendar = 0, kWorldSpace = 0, kUiSingleton = 0;
uint64_t g_frame = 0;
EngineView g_view{};
std::mutex g_m;
using SetCamFn = void (*)(const float*, const float*, float, uint32_t, uint32_t, uint32_t);
using SetWorldFn = void (*)(float, uint32_t, uint32_t);
SetCamFn g_setCam = nullptr;
SetWorldFn g_setWorld = nullptr;

template <class T> T At(const uint8_t* p, uint32_t off) { T v; std::memcpy(&v, p + off, sizeof v); return v; }

bool ReadScreen(EngineView* v) {
    __try {
        if (!kGraphicsState) return false;
        const uint8_t* gs = g_base + kGraphicsState;
        v->screen[0] = At<uint32_t>(gs, 0x24);
        v->screen[1] = At<uint32_t>(gs, 0x28);
        v->viewport[0] = At<uint32_t>(gs, 0x2C);
        v->viewport[1] = At<uint32_t>(gs, 0x30);
        return v->screen[0] > 0 && v->screen[0] < 16384 && v->screen[1] > 0 && v->screen[1] < 16384;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool ReadCamera(EngineView* v) {
    __try {
        if (!kPlayerCamera) return false;
        const uint8_t* cam = At<const uint8_t*>(g_base, kPlayerCamera);
        if (!cam) return false;
        const uint8_t* root = At<const uint8_t*>(cam, 0x20);
        if (!root) return false;
        std::memcpy(v->camRot, root + 0x7C, 36);
        std::memcpy(v->camPos, root + 0xA0, 12);
        v->fov = At<float>(cam, 0x13C);
        v->fovFirstPerson = At<float>(cam, 0x140);
        // PlayerCamera::currentState (+0x38) -> TESCameraState::id (+0x18): 0=1stP, 3=Free, 9=3rdP, etc.
        v->camStateId = 0xFFFFFFFFu;
        if (const uint8_t* st = At<const uint8_t*>(cam, 0x38)) {
            const uint32_t sid = At<uint32_t>(st, 0x18);
            if (sid <= 16u) v->camStateId = sid;
        }
        // a rotation's rows are unit length: anything else is a wrong offset, not a camera
        const float r0 = v->camRot[0] * v->camRot[0] + v->camRot[1] * v->camRot[1] + v->camRot[2] * v->camRot[2];
        return std::fabs(r0 - 1.0f) < 0.02f && v->fov > 1.0f && v->fov < 179.0f;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool ReadCell(EngineView* v) {
    __try {
        if (!kPlayer) return false;
        const uint8_t* pc = At<const uint8_t*>(g_base, kPlayer);
        if (!pc) return false;
        std::memcpy(v->playerPos, pc + 0x54, 12);
        const uint8_t* cell = At<const uint8_t*>(pc, 0x60);
        if (!cell) return false;
        v->cellFormId = At<uint32_t>(cell, 0x14);
        v->interior = At<uint16_t>(cell, 0x40) & 1u;
        // TESObjectCELL::waterHeight (+0x54) with TESWorldSpace::defaultWaterHeight (+0x150) fallback.
        v->waterHeight = -14000.0f;
        const float cellWh = At<float>(cell, 0x54);
        const bool cellWhValid = std::isfinite(cellWh) && cellWh > -50000.0f && cellWh < 50000.0f;
        if (cellWhValid) v->waterHeight = cellWh;
        // the worldspace: off:TESObjectCELL::worldSpace (+0x128 on 1.6.1170, a cell dump in game, ec7-2; CommonLib's
        // +0x120 held zero), checked by its form type (TESWorldSpace, 71)
        v->worldspaceFormId = 0;
        for (uint32_t off : {kWorldSpace, 0x128u, 0x120u}) {
            if (!off) continue;
            const uint8_t* ws = v->interior ? nullptr : At<const uint8_t*>(cell, off);
            if (ws && ws[0x1A] == 71) {
                v->worldspaceFormId = At<uint32_t>(ws, 0x14);
                if (!cellWhValid) {
                    const float wsWh = At<float>(ws, 0x150);
                    if (std::isfinite(wsWh) && wsWh > -50000.0f && wsWh < 50000.0f) v->waterHeight = wsWh;
                }
                break;
            }
        }
        // UI::Singleton->numPausesGame (+0x160): exact engine menu-pause state (MapMenu / Inventory / Journal / Pause).
        v->menuPaused = 0;
        if (kUiSingleton) {
            if (const uint8_t* ui = At<const uint8_t*>(g_base, kUiSingleton)) {
                const uint32_t np = At<uint32_t>(ui, 0x160);
                if (np > 0 && np < 64u) v->menuPaused = np;
            }
        }
        return v->cellFormId != 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool ReadTime(EngineView* v) {
    __try {
        using GetSky = const uint8_t* (*)();
        if (!kGetSky || !kCalendar) return false;
        const uint8_t* sky = reinterpret_cast<GetSky>(g_base + kGetSky)();
        if (!sky) return false;
        v->gameHour = At<float>(sky, 0x1B0);
        const uint8_t* days = At<const uint8_t*>(g_base + kCalendar, 0x28);
        v->daysPassed = days ? At<float>(days, 0x34) : 0.0f;
        return v->gameHour >= 0.0f && v->gameHour <= 24.0f;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

}  // namespace

void ViewInit(const Image& img) {
    g_base = img.base;
    kGraphicsState = Addr("BSGraphics::State::GetSingleton"); kPlayerCamera = Addr("PlayerCamera::Singleton");
    kPlayer = Addr("PlayerCharacter::Singleton"); kGetSky = Addr("fn:Sky::GetSingleton"); kCalendar = Addr("Calendar::Singleton");
    kWorldSpace = Off("off:TESObjectCELL::worldSpace");
    kUiSingleton = Addr("UI::Singleton");
    if (HMODULE proxy = GetModuleHandleA("d3d11.dll")) {
        g_setCam = reinterpret_cast<SetCamFn>(GetProcAddress(proxy, "PG_SetEngineCamera"));
        g_setWorld = reinterpret_cast<SetWorldFn>(GetProcAddress(proxy, "PG_SetEngineWorld"));
    }
}

void ViewOnPresent() {
    if (!g_base) return;
    EngineView v{};
    v.ok = (ReadScreen(&v) ? 1u : 0u) | (ReadCamera(&v) ? 2u : 0u) | (ReadCell(&v) ? 4u : 0u) | (ReadTime(&v) ? 8u : 0u);
    if (g_setCam && (v.ok & 6u)) {
        g_setCam(v.camPos, v.camRot, v.fov, v.interior, v.cellFormId, v.worldspaceFormId);
    }
    if (g_setWorld && (v.ok & 6u)) {
        g_setWorld(v.waterHeight, v.camStateId, v.menuPaused);
    }
    std::lock_guard<std::mutex> g(g_m);
    g_view = v;
    ++g_frame;
}

std::string ViewJson() {
    EngineView v;
    { std::lock_guard<std::mutex> g(g_m); v = g_view; }
    char b[896];
    std::snprintf(b, sizeof b,
                  "\"view_ok\":%u,\"screen\":[%u,%u],\"fb_viewport\":[%u,%u],\"cam_pos\":[%.1f,%.1f,%.1f],"
                  "\"cam_fwd\":[%.3f,%.3f,%.3f],\"fov\":%.2f,\"fov_1st\":%.2f,\"player_pos\":[%.1f,%.1f,%.1f],"
                  "\"cell\":\"0x%08X\",\"worldspace\":\"0x%08X\",\"cell_interior\":%u,\"water_height\":%.1f,"
                  "\"cam_state\":%u,\"menu_paused\":%u,\"game_hour\":%.3f,\"days_passed\":%.3f",
                  v.ok, v.screen[0], v.screen[1], v.viewport[0], v.viewport[1], v.camPos[0], v.camPos[1], v.camPos[2],
                  v.camRot[1], v.camRot[4], v.camRot[7], v.fov, v.fovFirstPerson, v.playerPos[0], v.playerPos[1],
                  v.playerPos[2], v.cellFormId, v.worldspaceFormId, v.interior, v.waterHeight,
                  v.camStateId, v.menuPaused, v.gameHour, v.daysPassed);
    return b;
}

}  // namespace raw::rt

extern "C" __declspec(dllexport) uint64_t RAW_EngineView(raw::rt::EngineView* out) {
    std::lock_guard<std::mutex> g(raw::rt::g_m);
    if (out) *out = raw::rt::g_view;
    return raw::rt::g_frame;
}
