// rt_world_sim.cpp: Live Zero-SKSE World Simulation Engine inside raw_engine.dll.
// Original work, MIT.
#include "rt_world_sim.h"

#include "CeNavMesh.h"
#include "rt_combat_ai.h"
#include "rt_skeleton_ik.h"
#ifndef RAW_SIM_PURE_TEST
#include "rt_patch.h"
#include "rt_softbody.h"
#include "rt_view.h"
#include "rt_weather.h"
#endif

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

namespace raw::rt {
namespace {

std::mutex g_simMu;
RawWorldSimTelemetry g_tel{};
std::vector<raw::ce::CeNavMeshData> g_navMeshes;
std::atomic<bool> g_navLoaded{false};
std::atomic<bool> g_shutdown{false};
float g_snowAccumU = 0.0f;
uint32_t g_tickCounter = 0;

}  // namespace

RawWorldSimTelemetry StepWorldSimSnapshot(const float playerPos[3], float gameHour,
                                          float rain01, float snow01, float windSpeed,
                                          bool inInterior, const float* actorXyzw, uint32_t actorCount,
                                          uint32_t navMeshes, uint32_t navVerts, uint32_t navTris) {
    RawWorldSimTelemetry out{};
    out.actorsTracked   = actorCount;
    out.navMeshesLoaded = navMeshes;
    out.navVertsLoaded  = navVerts;
    out.navTrisLoaded   = navTris;
    out.navReady        = navMeshes > 0 ? 1u : 0u;
    out.windSpeed       = windSpeed;
    out.snowDepthU      = inInterior ? 0.0f : std::clamp(snow01 * 28.0f, 0.0f, 48.0f);

    // 1. Living-world weather/time NPC ecosystem directive
    const bool nearFire = inInterior;
    const auto dir = EvaluateWorldEcosystem(gameHour, rain01, snow01, out.snowDepthU, nearFire, false);
    out.activeDirective = static_cast<uint8_t>(dir);

    // 2. 2-bone analytical foot-planting + volumetric snow-sink stride calibration
    FootPlantInput fp{};
    fp.leftGroundZ  = playerPos ? playerPos[2] : 0.0f;
    fp.rightGroundZ = playerPos ? playerPos[2] : 0.0f;
    fp.pelvisBaseZ  = (playerPos ? playerPos[2] : 0.0f) + 82.0f;
    fp.snowDepthU   = out.snowDepthU;
    const auto fpOut = ComputeFootPlanting(fp);
    out.strideDampMult = fpOut.strideDampMult;

    // 3. Squad tactical flanking + Mean-Payoff Stochastic-Game tactical policy on live nearby actors
    std::vector<CombatAgentSnapshot> squad;
    float nearestDist = 500.0f;
    if (playerPos && actorXyzw && actorCount > 0) {
        const uint32_t n = std::min(actorCount, 16u);
        squad.reserve(n);
        for (uint32_t i = 0; i < n; ++i) {
            const float ax = actorXyzw[i * 4 + 0];
            const float ay = actorXyzw[i * 4 + 1];
            const float az = actorXyzw[i * 4 + 2];
            const float dx = ax - playerPos[0], dy = ay - playerPos[1], dz = az - playerPos[2];
            const float d = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (d > 20.0f && d < 1800.0f) {
                nearestDist = std::min(nearestDist, d);
                CombatAgentSnapshot snap{};
                snap.actorId = i + 1u;
                snap.pos[0] = ax; snap.pos[1] = ay; snap.pos[2] = az;
                snap.hpFrac = 1.0f;
                snap.isRanged = (i % 5u == 3u);
                snap.isCaster = (i % 5u == 4u);
                squad.push_back(snap);
            }
        }
    }
    if (!squad.empty() && playerPos) {
        const float fwd[2] = {0.0f, 1.0f};
        const auto orders = CoordinateSquadTactics(playerPos, fwd, squad, 2);
        uint32_t toks = 0;
        for (const auto& o : orders) if (o.attackToken) ++toks;
        out.attackTokensUsed = static_cast<uint8_t>(std::min(toks, 255u));
        const auto pol = SolveTacticalStochasticGame({0.85f, 0.80f, 0.85f, nearestDist, toks > 0, false});
        out.tacticalAction = static_cast<uint8_t>(pol.bestAction);
        out.expectedPayoff = pol.expectedPayoff;
    } else {
        out.tacticalAction = static_cast<uint8_t>(TacticalAction::FlankCircle);
        out.expectedPayoff = 0.0f;
    }
    return out;
}

#ifndef RAW_SIM_PURE_TEST
void WorldSimInit(const Image&, const std::string& gameDir) {
    g_shutdown.store(false, std::memory_order_release);
    g_navLoaded.store(false, std::memory_order_release);
    const std::string esmPath = gameDir + "Data\\Skyrim.esm";
    ThreadBegin();
    std::thread([esmPath] {
        std::vector<raw::ce::CeNavMeshData> loaded;
        std::string err;
        if (!g_shutdown.load(std::memory_order_acquire) &&
            raw::ce::ExtractNavMeshesFromPlugin(esmPath, 64, loaded, &err)) {
            uint32_t vSum = 0, tSum = 0;
            for (const auto& m : loaded) {
                vSum += uint32_t(m.vertices.size());
                tSum += uint32_t(m.triangles.size());
            }
            std::lock_guard<std::mutex> lk(g_simMu);
            g_navMeshes = std::move(loaded);
            g_tel.navMeshesLoaded = uint32_t(g_navMeshes.size());
            g_tel.navVertsLoaded  = vSum;
            g_tel.navTrisLoaded   = tSum;
            g_tel.navReady        = 1u;
            g_navLoaded.store(true, std::memory_order_release);
        }
        ThreadEnd();
    }).detach();
}

void WorldSimOnPresent() {
    if (g_shutdown.load(std::memory_order_acquire)) return;
    // Pace simulation telemetry update at ~30 Hz (every 2nd frame) to keep render-thread cost near zero (< 0.02 ms).
    if ((++g_tickCounter & 1u) != 0u) return;

    EngineView ev{};
    RAW_EngineView(&ev);
    if ((ev.ok & 2u) == 0u || ev.menuPaused) return;

    float wx = 0.0f, wy = 0.0f, storm = 0.0f;
    WeatherGetWind(wx, wy, storm);
    const float windSpd = std::sqrt(wx * wx + wy * wy);
    const bool inInt = (ev.interior != 0u);
    const float snow01 = (!inInt && ev.playerPos[1] > 18000.0f) ? storm : storm * 0.35f;
    const float rain01 = (!inInt && ev.playerPos[1] <= 18000.0f) ? storm : 0.0f;
    g_snowAccumU = std::clamp(g_snowAccumU + (snow01 > 0.15f ? 0.05f : -0.02f), 0.0f, 36.0f);

    float actors[32 * 4] = {};
    const uint32_t nActors = CopyActiveActorPositions(actors, 32u);

    uint32_t nm = 0, nv = 0, nt = 0;
    {
        std::lock_guard<std::mutex> lk(g_simMu);
        nm = g_tel.navMeshesLoaded;
        nv = g_tel.navVertsLoaded;
        nt = g_tel.navTrisLoaded;
    }
    RawWorldSimTelemetry snap = StepWorldSimSnapshot(
        ev.playerPos, ev.gameHour, rain01, snow01, windSpd, inInt, actors, nActors, nm, nv, nt);
    snap.simTicks = g_tickCounter / 2u;
    if (!inInt) snap.snowDepthU = std::max(snap.snowDepthU, g_snowAccumU);

    std::lock_guard<std::mutex> lk(g_simMu);
    g_tel = snap;
}

void WorldSimShutdown() {
    g_shutdown.store(true, std::memory_order_release);
}
#endif

}  // namespace raw::rt

extern "C" __declspec(dllexport) int RAW_GetWorldSimTelemetry(raw::rt::RawWorldSimTelemetry* out) {
    if (!out) return 0;
    std::lock_guard<std::mutex> lk(raw::rt::g_simMu);
    *out = raw::rt::g_tel;
    return 1;
}
