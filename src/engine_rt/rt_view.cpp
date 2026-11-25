// The view: camera, cell, time and the screen (RAW engine runtime). Original work, MIT. See rt_view.h.
#include "rt_view.h"
#include "rt_memory_guards.h"
#include "rt_version.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <unordered_map>

namespace raw::rt {
namespace {

const uint8_t* g_base = nullptr;
// from the version table at ViewInit; a part whose address is 0 reads as not available
uint32_t kGraphicsState = 0, kPlayerCamera = 0, kPlayer = 0, kGetSky = 0, kCalendar = 0, kWorldSpace = 0, kUiSingleton = 0;
uint32_t kCellWaterHeightOff = 0x78u, kWorldDefaultWaterOff = 0x214u;
uint64_t g_frame = 0;
EngineView g_view{};
std::mutex g_m;
std::atomic<bool> g_sweepActiveFlag{false};
struct SavedCellWater {
    uintptr_t cellAddr = 0;
    float     baseHeight = -14000.0f;
};
std::mutex g_tideMu;
std::unordered_map<uint32_t, SavedCellWater> g_baseCellWater;
uintptr_t g_activeExtCellAddr = 0;
uint32_t  g_activeExtCellId = 0;
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
        // TESCamera::currentState (+0x28) -> TESCameraState::id (+0x18): 0=1stP, 3=Free, 9=3rdP, 10=Mount, etc.
        v->camStateId = 0xFFFFFFFFu;
        if (const uint8_t* st = At<const uint8_t*>(cam, 0x28)) {
            const uint32_t sid = At<uint32_t>(st, 0x18);
            if (sid <= 16u) v->camStateId = sid;
        }
        // a rotation's rows are unit length: anything else is a wrong offset, not a camera
        const float r0 = v->camRot[0] * v->camRot[0] + v->camRot[1] * v->camRot[1] + v->camRot[2] * v->camRot[2];
        return std::fabs(r0 - 1.0f) < 0.02f && v->fov > 1.0f && v->fov < 179.0f;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool SehWriteCellWaterIfMatch(uintptr_t cellAddr, uint32_t expectedFormId, float newWaterHeight) {
    __try {
        if (!cellAddr || !expectedFormId || !std::isfinite(newWaterHeight)) return false;
        auto* cell = reinterpret_cast<uint8_t*>(cellAddr);
        if (At<uint32_t>(cell, 0x14) != expectedFormId) return false;
        const uint16_t flags = At<uint16_t>(cell, 0x40);
        const uint8_t cellState = At<uint8_t>(cell, 0x44);
        const uint32_t spinLockCount = At<uint32_t>(cell, 0x124);
        // Binary-verified SkyrimSE 1.7.104 TESObjectCELL::GetExteriorWaterHeight (RVA 0x2BEE00):
        //   +0x68 = CELL_DATA* (EXTERIOR_DATA*), +0x70 = TESObjectLAND* (NEVER touch!)
        //   +0x78 = float waterHeight (sentinel 2147483648.0f when inheriting worldspace default)
        const float cur = At<float>(cell, kCellWaterHeightOff);
        CellWriteRequest req{};
        req.cellFlags = flags;
        req.proposedCellFlags = flags;
        req.cellState = cellState;
        req.spinLockCount = spinLockCount;
        req.writeOffset = kCellWaterHeightOff;
        req.currentWaterHeight = cur;
        req.proposedWaterHeight = newWaterHeight;
        req.binaryOffsetsVerified = true;
        if (!MemoryGuardCheckCellWrite(req).allowed) return false;
        if (std::fabs(cur - newWaterHeight) > 0.02f) {
            std::memcpy(cell + kCellWaterHeightOff, &newWaterHeight, sizeof(float));
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool ReadCell(EngineView* v, uintptr_t* outExtCellAddr, bool* outCellWhValid) {
    __try {
        if (outExtCellAddr) *outExtCellAddr = 0;
        if (outCellWhValid) *outCellWhValid = false;
        if (!kPlayer) return false;
        const uint8_t* pc = At<const uint8_t*>(g_base, kPlayer);
        if (!pc) return false;
        std::memcpy(v->playerPos, pc + 0x54, 12);
        const uint8_t* cell = At<const uint8_t*>(pc, 0x60);
        if (!cell) return false;
        v->cellFormId = At<uint32_t>(cell, 0x14);
        const uint16_t cellFlags = At<uint16_t>(cell, 0x40);
        v->interior = cellFlags & 1u;
        const bool hasWater = (cellFlags & 2u) != 0u;
        // Binary-verified SkyrimSE 1.7.104 offsets:
        //   TESObjectCELL::waterHeight (+0x78, sentinel 2147483648.0f when inheriting worldspace)
        //   TESWorldSpace::defaultWaterHeight (+0x214, RVA 0x30DE80)
        v->waterHeight = -14000.0f;
        const float cellWh = At<float>(cell, kCellWaterHeightOff);
        const bool cellWhValid = hasWater && std::isfinite(cellWh) && cellWh > -50000.0f && cellWh < 50000.0f;
        if (cellWhValid) v->waterHeight = cellWh;
        if (!v->interior && cellWhValid && outExtCellAddr && outCellWhValid) {
            *outExtCellAddr = reinterpret_cast<uintptr_t>(cell);
            *outCellWhValid = true;
        }
        // the worldspace: off:TESObjectCELL::worldSpace (+0x128 on 1.6.1170/1.7.104), checked by form type (71)
        v->worldspaceFormId = 0;
        for (uint32_t off : {kWorldSpace, 0x128u, 0x120u}) {
            if (!off) continue;
            const uint8_t* ws = v->interior ? nullptr : At<const uint8_t*>(cell, off);
            if (ws && ws[0x1A] == 71) {
                v->worldspaceFormId = At<uint32_t>(ws, 0x14);
                if (hasWater && !cellWhValid) {
                    const float wsWh = At<float>(ws, kWorldDefaultWaterOff);
                    if (std::isfinite(wsWh) && wsWh > -50000.0f && wsWh < 50000.0f) v->waterHeight = wsWh;
                }
                break;
            }
        }
        // UI::Singleton->numPausesGame (+0x160) and menuStack._size (+0x120, binary-verified 1.7.104 at 0x760627):
        v->menuPaused = 0;
        v->menuStackDepth = 0;
        if (kUiSingleton) {
            if (const uint8_t* ui = At<const uint8_t*>(g_base, kUiSingleton)) {
                const uint32_t np = At<uint32_t>(ui, 0x160);
                if (np > 0 && np < 64u) v->menuPaused = np;
                const uint32_t stackCap = At<uint32_t>(ui, 0x118);
                const uint32_t stackSz  = At<uint32_t>(ui, 0x120);
                if (stackSz < 64u && stackCap < 256u && stackSz <= stackCap) {
                    v->menuStackDepth = stackSz;
                }
            }
        }
        return v->cellFormId != 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool ReadTime(EngineView* v) {
    __try {
        using GetSky = const uint8_t* (*)();
        v->gameYear   = 201.0f;
        v->gameMonth  = 7.0f;
        v->gameDay    = 17.0f;
        v->gameHour   = 12.0f;
        v->daysPassed = 0.0f;
        v->timeScale  = 20.0f;
        bool hasHour = false;
        if (kGetSky) {
            if (const uint8_t* sky = reinterpret_cast<GetSky>(g_base + kGetSky)()) {
                const float gh = At<float>(sky, 0x1B0);
                if (std::isfinite(gh) && gh >= 0.0f && gh <= 24.0f) {
                    v->gameHour = gh;
                    hasHour = true;
                }
            }
        }
        // Binary-verified SkyrimSE 1.7.104.0 Calendar::Singleton (0x21A05D0) is a Calendar* POINTER:
        //   +0x08 gameYear, +0x10 gameMonth, +0x18 gameDay, +0x20 gameHour, +0x28 gameDaysPassed, +0x30 timeScale
        //   Each points to a TESGlobal whose float value sits at +0x34 (proven in 0x645C80, 0x645EF0..0x646160).
        if (kCalendar) {
            if (const uint8_t* cal = At<const uint8_t*>(g_base, kCalendar)) {
                auto readGlobal = [](const uint8_t* c, uint32_t off, float fallback) -> float {
                    const uint8_t* g = At<const uint8_t*>(c, off);
                    if (!g) return fallback;
                    const float val = At<float>(g, 0x34);
                    return std::isfinite(val) ? val : fallback;
                };
                v->gameYear   = readGlobal(cal, 0x08, v->gameYear);
                v->gameMonth  = std::clamp(readGlobal(cal, 0x10, v->gameMonth), 0.0f, 11.0f);
                v->gameDay    = std::clamp(readGlobal(cal, 0x18, v->gameDay), 1.0f, 31.0f);
                const float calHour = readGlobal(cal, 0x20, v->gameHour);
                if (!hasHour && calHour >= 0.0f && calHour <= 24.0f) {
                    v->gameHour = calHour;
                    hasHour = true;
                }
                v->daysPassed = std::max(readGlobal(cal, 0x28, 0.0f), 0.0f);
                v->timeScale  = std::clamp(readGlobal(cal, 0x30, 20.0f), 0.0f, 1000.0f);
            }
        }
    return hasHour && v->gameHour >= 0.0f && v->gameHour <= 24.0f;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool SehApplyCameraSweepStep(float dYawRad, float dPitchRad, float dFwdUnits) {
    __try {
        if (!g_base || !kPlayer || !kPlayerCamera) return false;
        const uint8_t* cam = At<const uint8_t*>(g_base, kPlayerCamera);
        if (!cam) return false;
        const uint8_t* camRoot = At<const uint8_t*>(cam, 0x20);
        if (!camRoot) return false;

        uint8_t* pc = const_cast<uint8_t*>(At<const uint8_t*>(g_base, kPlayer));
        if (!pc) return false;
        const uint8_t* cell = At<const uint8_t*>(pc, 0x60);
        if (!cell) return false;
        const uint32_t cellId = At<uint32_t>(cell, 0x14);
        if (cellId == 0u) return false;

        if (kUiSingleton) {
            if (const uint8_t* ui = At<const uint8_t*>(g_base, kUiSingleton)) {
                const uint32_t np = At<uint32_t>(ui, 0x160);
                if (np > 0u && np < 64u) return false;
            }
        }

        float rot[3]{};
        float pos[3]{};
        std::memcpy(rot, pc + 0x48, sizeof rot);
        std::memcpy(pos, pc + 0x54, sizeof pos);

        CameraPose pose{};
        pose.pitchRad = rot[0];
        pose.rollRad  = rot[1];
        pose.yawRad   = rot[2];
        pose.pos[0]   = pos[0];
        pose.pos[1]   = pos[1];
        pose.pos[2]   = pos[2];
        if (!ComputeCameraSweepStep(pose, dYawRad, dPitchRad, dFwdUnits)) return false;

        rot[0] = pose.pitchRad;
        rot[1] = pose.rollRad;
        rot[2] = pose.yawRad;
        std::memcpy(pc + 0x48, rot, sizeof rot);
        if (std::fabs(dFwdUnits) > 1.0e-6f) {
            std::memcpy(pc + 0x54, pose.pos, sizeof pose.pos);
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool SehSetPlayerPose(float x, float y, float z, float pitchRad, float yawRad) {
    __try {
        if (!g_base || !kPlayer || !kPlayerCamera) return false;
        const uint8_t* cam = At<const uint8_t*>(g_base, kPlayerCamera);
        if (!cam) return false;
        const uint8_t* camRoot = At<const uint8_t*>(cam, 0x20);
        if (!camRoot) return false;

        uint8_t* pc = const_cast<uint8_t*>(At<const uint8_t*>(g_base, kPlayer));
        if (!pc) return false;
        const uint8_t* cell = At<const uint8_t*>(pc, 0x60);
        if (!cell) return false;
        if (At<uint32_t>(cell, 0x14) == 0u) return false;

        if (kUiSingleton) {
            if (const uint8_t* ui = At<const uint8_t*>(g_base, kUiSingleton)) {
                const uint32_t np = At<uint32_t>(ui, 0x160);
                if (np > 0u && np < 64u) return false;
            }
        }

        CameraPose pose{};
        if (!ComputeSetPlayerPose(pose, x, y, z, pitchRad, yawRad)) return false;

        const float rot[3] = {pose.pitchRad, pose.rollRad, pose.yawRad};
        std::memcpy(pc + 0x48, rot, sizeof rot);
        std::memcpy(pc + 0x54, pose.pos, sizeof pose.pos);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool SehSetGameHour(float hour) {
    __try {
        float clampedHour = 0.0f;
        if (!ComputeSetGameHour(clampedHour, hour)) return false;
        if (!g_base) return false;
        bool wroteAny = false;
        // Write Calendar::Singleton (+0x20 gameHour TESGlobal* -> +0x34 float value):
        if (kCalendar) {
            if (const uint8_t* cal = At<const uint8_t*>(g_base, kCalendar)) {
                if (uint8_t* gh = const_cast<uint8_t*>(At<const uint8_t*>(cal, 0x20))) {
                    const float cur = At<float>(gh, 0x34);
                    if (std::isfinite(cur) && cur >= 0.0f && cur <= 24.5f) {
                        std::memcpy(gh + 0x34, &clampedHour, sizeof(float));
                        wroteAny = true;
                    }
                }
            }
        }
        // Synchronize live Sky::currentGameHour (+0x1B0) immediately for sun/moon/lighting continuity:
        if (kGetSky) {
            using GetSky = uint8_t* (*)();
            if (uint8_t* sky = reinterpret_cast<GetSky>(const_cast<uint8_t*>(g_base) + kGetSky)()) {
                const float cur = At<float>(sky, 0x1B0);
                if (std::isfinite(cur) && cur >= 0.0f && cur <= 24.5f) {
                    std::memcpy(sky + 0x1B0, &clampedHour, sizeof(float));
                    wroteAny = true;
                }
            }
        }
        return wroteAny;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

}  // namespace

void ViewInit(const Image& img) {
    g_base = img.base;
    kGraphicsState = Addr("BSGraphics::State::GetSingleton"); kPlayerCamera = Addr("PlayerCamera::Singleton");
    kPlayer = Addr("PlayerCharacter::Singleton"); kGetSky = Addr("fn:Sky::GetSingleton"); kCalendar = Addr("Calendar::Singleton");
    kWorldSpace = Off("off:TESObjectCELL::worldSpace");
    if (const uint32_t wh = Off("off:TESObjectCELL::waterHeight")) kCellWaterHeightOff = wh;
    if (const uint32_t dw = Off("off:TESWorldSpace::defaultWaterHeight")) kWorldDefaultWaterOff = dw;
    kUiSingleton = Addr("UI::Singleton");
    if (HMODULE proxy = GetModuleHandleA("d3d11.dll")) {
        g_setCam = reinterpret_cast<SetCamFn>(GetProcAddress(proxy, "PG_SetEngineCamera"));
        g_setWorld = reinterpret_cast<SetWorldFn>(GetProcAddress(proxy, "PG_SetEngineWorld"));
    }
}

void ViewOnPresent() {
    if (!g_base) return;
    EngineView v{};
    uintptr_t extCellAddr = 0;
    bool extCellWhValid = false;
    v.ok = (ReadScreen(&v) ? 1u : 0u) | (ReadCamera(&v) ? 2u : 0u) |
           (ReadCell(&v, &extCellAddr, &extCellWhValid) ? 4u : 0u) | (ReadTime(&v) ? 8u : 0u);
    {
        std::lock_guard<std::mutex> tl(g_tideMu);
        if ((v.ok & 4u) && !v.interior && extCellWhValid && extCellAddr && v.cellFormId != 0u) {
            auto it = g_baseCellWater.find(v.cellFormId);
            if (it == g_baseCellWater.end() || it->second.cellAddr != extCellAddr) {
                g_baseCellWater[v.cellFormId] = {extCellAddr, v.waterHeight};
            } else {
                // Report the clean baseline height so GPU shaders add tide once without recursive feedback drift.
                v.waterHeight = it->second.baseHeight;
            }
            g_activeExtCellAddr = extCellAddr;
            g_activeExtCellId = v.cellFormId;
        } else {
            g_activeExtCellAddr = 0;
            g_activeExtCellId = 0;
        }
    }
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

void ViewApplyLiveTide(float tideHeightU) {
    uintptr_t cellAddr = 0;
    uint32_t cellId = 0;
    float baseH = -14000.0f;
    {
        std::lock_guard<std::mutex> tl(g_tideMu);
        if (!g_activeExtCellAddr || !g_activeExtCellId) return;
        auto it = g_baseCellWater.find(g_activeExtCellId);
        if (it == g_baseCellWater.end() || it->second.cellAddr != g_activeExtCellAddr) return;
        cellAddr = it->second.cellAddr;
        cellId = g_activeExtCellId;
        baseH = it->second.baseHeight;
    }
    // Strictly restrict live CE tidal cell writes to coastal sea-level cells (Z ~ -14000u) with gentle +-4u clamp
    // so inland Whiterun pools and Riverwood rivers NEVER flood stairs or empty below their beds!
    if (std::fabs(baseH - (-14000.0f)) > 350.0f) return;
    const float clampedTide = std::isfinite(tideHeightU) ? std::clamp(tideHeightU * 0.25f, -4.0f, 4.0f) : 0.0f;
    SehWriteCellWaterIfMatch(cellAddr, cellId, baseH + clampedTide);
}

void ViewRestoreAllTides() {
    std::unordered_map<uint32_t, SavedCellWater> snap;
    {
        std::lock_guard<std::mutex> tl(g_tideMu);
        snap.swap(g_baseCellWater);
        g_activeExtCellAddr = 0;
        g_activeExtCellId = 0;
    }
    for (const auto& [formId, saved] : snap) {
        SehWriteCellWaterIfMatch(saved.cellAddr, formId, saved.baseHeight);
    }
}

bool ApplyCameraSweepStep(float dYawRad, float dPitchRad, float dFwdUnits) {
    return SehApplyCameraSweepStep(dYawRad, dPitchRad, dFwdUnits);
}

bool SetPlayerPose(float x, float y, float z, float pitchRad, float yawRad) {
    return SehSetPlayerPose(x, y, z, pitchRad, yawRad);
}

bool SetGameHour(float hour) {
    return SehSetGameHour(hour);
}

bool IsMenuPausedOrModal() noexcept {
    std::lock_guard<std::mutex> g(g_m);
    return (g_view.menuPaused > 0u);
}

bool IsCameraSweepActive() noexcept {
    return g_sweepActiveFlag.load(std::memory_order_acquire);
}

void SetCameraSweepActiveFlag(bool active) noexcept {
    g_sweepActiveFlag.store(active, std::memory_order_release);
}

std::string ViewJson() {
    EngineView v;
    { std::lock_guard<std::mutex> g(g_m); v = g_view; }
    const uint32_t sweepAct = IsCameraSweepActive() ? 1u : 0u;
    char b[1280];
    std::snprintf(b, sizeof b,
                  "\"view_ok\":%u,\"screen\":[%u,%u],\"fb_viewport\":[%u,%u],\"cam_pos\":[%.1f,%.1f,%.1f],"
                  "\"cam_fwd\":[%.3f,%.3f,%.3f],\"fov\":%.2f,\"fov_1st\":%.2f,\"player_pos\":[%.1f,%.1f,%.1f],"
                  "\"cell\":\"0x%08X\",\"cell_id\":%u,\"worldspace\":\"0x%08X\",\"cell_interior\":%u,\"interior\":%u,"
                  "\"water_height\":%.1f,\"cam_state\":%u,\"menu_paused\":%u,\"menu_stack_depth\":%u,\"sweep_active\":%u,"
                  "\"game_year\":%.0f,\"game_month\":%.0f,\"game_day\":%.0f,"
                  "\"game_hour\":%.3f,\"days_passed\":%.3f,\"time_scale\":%.2f",
                  v.ok, v.screen[0], v.screen[1], v.viewport[0], v.viewport[1], v.camPos[0], v.camPos[1], v.camPos[2],
                  v.camRot[1], v.camRot[4], v.camRot[7], v.fov, v.fovFirstPerson, v.playerPos[0], v.playerPos[1],
                  v.playerPos[2], v.cellFormId, v.cellFormId, v.worldspaceFormId, v.interior, v.interior,
                  v.waterHeight, v.camStateId, v.menuPaused, v.menuStackDepth, sweepAct,
                  v.gameYear, v.gameMonth, v.gameDay,
                  v.gameHour, v.daysPassed, v.timeScale);
    return b;
}

}  // namespace raw::rt

extern "C" __declspec(dllexport) uint64_t RAW_EngineView(raw::rt::EngineView* out) {
    std::lock_guard<std::mutex> g(raw::rt::g_m);
    if (out) *out = raw::rt::g_view;
    return raw::rt::g_frame;
}

extern "C" __declspec(dllexport) int RAW_GetCalendarState(float* out6) {
    if (!out6) return 0;
    raw::rt::EngineView v;
    {
        std::lock_guard<std::mutex> g(raw::rt::g_m);
        v = raw::rt::g_view;
    }
    if ((v.ok & 8u) == 0u) return 0;
    out6[0] = v.gameYear;
    out6[1] = v.gameMonth;
    out6[2] = v.gameDay;
    out6[3] = v.gameHour;
    out6[4] = v.daysPassed;
    out6[5] = v.timeScale;
    return 1;
}
