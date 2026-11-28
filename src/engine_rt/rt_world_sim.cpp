// rt_world_sim.cpp: Live Zero-SKSE World Simulation Engine inside raw_engine.dll.
// Original work, MIT.
#include "rt_world_sim.h"

#include "CeNavMesh.h"
#include "rt_combat_ai.h"
#include "rt_economy_ledger.h"
#include "rt_skeleton_ik.h"
#include "rt_stealth_survival.h"
#ifndef RAW_SIM_PURE_TEST
#include "rt_control.h"
#include "rt_livestate.h"
#include "rt_patch.h"
#include "rt_softbody.h"
#include "rt_view.h"
#include "rt_weather.h"
#include <windows.h>
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
std::atomic<float> g_shiverAtomic{0.0f};
float g_snowAccumU = 0.0f;
float g_coreWarmth = 85.0f;
uint32_t g_tickCounter = 0;
uint64_t g_lastTickUs = 0;
uint32_t g_saveCount = 0;
float g_largestSaveMb = 0.0f;
bool g_saveBloatWarn = false;
ExtremumSeekingState g_gpuEsState{0.92f, 0.0f, 0.0f, false};
float g_simClockSec = 0.0f;

}  // namespace

float WorldSimGetShiverIntensity() {
    return g_shiverAtomic.load(std::memory_order_relaxed);
}

float ComputeBoundedTidalCellHeight(float baseWaterHeight, float tideHeightU, bool isInterior) {
    if (isInterior || !std::isfinite(baseWaterHeight) ||
        baseWaterHeight <= -13999.0f || baseWaterHeight >= 50000.0f || baseWaterHeight == 0.0f) {
        return baseWaterHeight;
    }
    const float clampedTide = std::isfinite(tideHeightU) ? std::clamp(tideHeightU, -24.0f, 24.0f) : 0.0f;
    return baseWaterHeight + clampedTide;
}

RawWorldSimTelemetry StepWorldSimSnapshot(const float playerPos[3], float gameHour,
                                          float rain01, float snow01, float windSpeed,
                                          bool inInterior, const float* actorXyzw, uint32_t actorCount,
                                          uint32_t navMeshes, uint32_t navVerts, uint32_t navTris,
                                          float frameDtSec, float prevWarmth) {
    RawWorldSimTelemetry out{};
    out.actorsTracked   = actorCount;
    out.navMeshesLoaded = navMeshes;
    out.navVertsLoaded  = navVerts;
    out.navTrisLoaded   = navTris;
    out.navReady        = navMeshes > 0 ? 1u : 0u;
    out.windSpeed       = windSpeed;
    out.snowDepthU      = inInterior ? 0.0f : std::clamp(snow01 * 28.0f, 0.0f, 48.0f);

    // 1. Living-world weather/time NPC shelter, hearth & torch ecosystem heuristic
    const bool nearFire = inInterior;
    NpcEcosystemInput npcIn{};
    npcIn.gameHour     = gameHour;
    npcIn.rain01       = rain01;
    npcIn.snow01       = snow01;
    npcIn.storm01      = std::max(rain01, snow01);
    npcIn.snowDepthU   = out.snowDepthU;
    npcIn.windSpeedMs  = windSpeed;
    npcIn.ambientTempC = inInterior ? 18.0f : (10.0f - snow01 * 14.0f);
    npcIn.inInterior   = inInterior;
    npcIn.nearFire     = nearFire;
    npcIn.inCombat     = false;
    npcIn.hold         = playerPos ? ClassifySkyrimHold(playerPos[0], playerPos[1]) : SkyrimHold::Whiterun;
    npcIn.nearbyActors = actorCount;
    const auto npcDec  = EvaluateNpcShelterTorchHeuristic(npcIn);
    out.activeDirective = static_cast<uint8_t>(npcDec.directive);

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

    // 4. High-FPS physics substep governor (30..360 Hz stable + 50 ms spike clamp)
    const auto physPlan = ComputePhysicsSubstep(frameDtSec);
    out.physicsSubsteps  = static_cast<uint8_t>(std::min<uint32_t>(physPlan.substeps, 16u));
    out.physicsSubstepMs = physPlan.substepDt * 1000.0f;

    // 5. Environmental thermal exposure & hypothermia survival state
    const float altZ = playerPos ? playerPos[2] : 0.0f;
    const auto therm = EvaluateThermalExposure(altZ, windSpeed, rain01, snow01,
                                               inInterior, nearFire, prevWarmth, physPlan.clampedFrameDt * 2.0f);
    out.ambientTempC     = therm.ambientTempC;
    out.coreWarmth       = therm.coreWarmth;
    out.shiverIntensity  = therm.shiverIntensity;
    out.staminaRegenMult = therm.staminaRegenMult;
    out.freezingRisk     = therm.freezingRisk ? 1u : 0u;

    // 6. Wave 40 Physical 3D Acoustic Environment Classifier (Sabine-Eyring RT60 + ISO 9613-1 HF Air Absorption):
    const float encRadU = inInterior ? 720.0f : (altZ > 2200.0f ? 2100.0f : 4200.0f);
    AcousticClassificationInput acIn{};
    acIn.inInterior       = inInterior;
    acIn.underwater       = false;
    acIn.isIceCavern      = inInterior && (playerPos ? (playerPos[1] > 24000.0f) : false);
    acIn.altitudeZ        = altZ;
    acIn.worldY           = playerPos ? playerPos[1] : 0.0f;
    acIn.snowCover01      = snow01;
    acIn.rain01           = rain01;
    acIn.windSpeedMs      = windSpeed;
    acIn.temperatureC     = therm.ambientTempC;
    acIn.humidity01       = std::clamp(0.45f + rain01 * 0.45f + snow01 * 0.25f, 0.15f, 0.98f);
    acIn.enclosureRadiusU = encRadU;
    const auto acProf = ClassifyAcousticEnvironment(acIn);
    out.acousticRt60Sec   = acProf.rt60Sec;
    out.acousticLowPassHz = acProf.lowPassHz;
    out.acousticEnvClass  = static_cast<uint8_t>(acProf.kind);

    // 7. Dynamic hold economy scarcity + smooth-follow combat camera FOV lock-on solver
    const auto econ = EvaluateDynamicEconomy(std::max(rain01, snow01), out.snowDepthU, gameHour, actorCount, inInterior);
    out.marketTier     = econ.marketTier;
    out.economyBuyMult = econ.buyPriceMult;
    const float* lockTarget = (!squad.empty()) ? squad[0].pos : nullptr;
    const auto camLock = SolveSmoothCameraLockOn(playerPos, playerPos, playerPos, lockTarget, 80.0f, 14.0f, physPlan.clampedFrameDt);
    out.camLockedOn  = camLock.lockedOn ? 1u : 0u;
    out.lockOnFovDeg = camLock.dynamicFovDeg;

    // 8. Wave 23 Deterministic Boruvka MST over active squad positions (detects formation cohesion vs split flanks)
    if (squad.size() >= 2u) {
        std::vector<MstEdge> edges;
        edges.reserve(squad.size() * (squad.size() - 1u) / 2u);
        for (size_t i = 0; i < squad.size(); ++i) {
            for (size_t j = i + 1; j < squad.size(); ++j) {
                const float dx = squad[i].pos[0] - squad[j].pos[0];
                const float dy = squad[i].pos[1] - squad[j].pos[1];
                const float dz = squad[i].pos[2] - squad[j].pos[2];
                const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
                if (dist < 1400.0f) edges.push_back({uint32_t(i), uint32_t(j), dist});
            }
        }
        const auto mst = ComputeBoruvkaMst(uint32_t(squad.size()), edges);
        out.squadSpanLengthU = mst.totalWeight;
        out.squadMstComps    = static_cast<uint8_t>(std::min<uint32_t>(mst.components, 255u));
        out.squadMstRounds   = static_cast<uint8_t>(std::min<uint32_t>(mst.rounds, 255u));
    } else {
        out.squadSpanLengthU = 0.0f;
        out.squadMstComps    = static_cast<uint8_t>(squad.size());
        out.squadMstRounds   = 0u;
    }

    // 9. Wave 27/28 Synoptic Barometric Storm Simulator snapshot + Wave 34 Planetary Gravity & Nirn Dual-Moon Tides:
    const auto syn = StepSynopticWeatherSim(playerPos, gameHour * 3600.0f, gameHour);
    out.synopticHpa       = syn.pressureHpa;
    out.adaptiveQuality   = 1.0f;
    out.synopticCloud     = syn.cloudCover01;
    out.synopticRain      = syn.rainRate01;
    out.synopticSnow      = syn.snowRate01;
    out.synopticLightning = syn.stormLightning;
    const auto plan = EvaluatePlanetaryPhysics(0.0f, gameHour, playerPos, std::max(windSpeed, syn.windSpeedMs), syn.pressureHpa);
    out.gravityMps2       = plan.gravityMps2;
    out.tideHeightU       = inInterior ? 0.0f : plan.tideHeightU;
    out.tideVelocityMs    = inInterior ? 0.0f : plan.tideVelocityMs;
    out.springTide01      = plan.springTide01;
    out.beaufortScale     = inInterior ? 0.5f : plan.beaufortScale;
    out.swellDirX         = plan.swellDirX;
    out.swellDirY         = plan.swellDirY;
    out.simClockSec       = gameHour * 3600.0f;
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
        // Background save-file health & bloat scan (honors Skyrim.ini sLocalSavePath such as __MO_Saves):
        char userProf[MAX_PATH]{};
        if (GetEnvironmentVariableA("USERPROFILE", userProf, MAX_PATH) > 0) {
            const std::string docsDir = std::string(userProf) + "\\Documents\\My Games\\Skyrim Special Edition";
            const std::string activeSavesDir = ResolveSkyrimSavesDir(docsDir.c_str());
            const std::string defaultSavesDir = docsDir + "\\Saves";
            auto sh = InspectSaveHealth(activeSavesDir.c_str());
            if (activeSavesDir != defaultSavesDir) {
                const auto shDef = InspectSaveHealth(defaultSavesDir.c_str());
                sh.totalSaves += shDef.totalSaves;
                sh.largestSaveMb = std::max(sh.largestSaveMb, shDef.largestSaveMb);
                sh.bloatWarning = sh.bloatWarning || shDef.bloatWarning;
            }
            std::lock_guard<std::mutex> lk(g_simMu);
            g_saveCount = sh.totalSaves;
            g_largestSaveMb = sh.largestSaveMb;
            g_saveBloatWarn = sh.bloatWarning;
            g_tel.totalSavesFound = sh.totalSaves;
            g_tel.largestSaveMb   = sh.largestSaveMb;
            g_tel.saveBloatWarn   = sh.bloatWarning ? 1u : 0u;
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
    LARGE_INTEGER freq{}, now{};
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&now);
    const uint64_t nowUs = freq.QuadPart > 0 ? uint64_t((now.QuadPart * 1000000ll) / freq.QuadPart) : 0;
    float tickDtSec = 1.0f / 30.0f;
    if (g_lastTickUs > 0 && nowUs > g_lastTickUs) {
        tickDtSec = std::clamp(float(double(nowUs - g_lastTickUs) * 1e-6), 0.002f, 0.25f);
    }
    g_lastTickUs = nowUs;

    RawWorldSimTelemetry snap = StepWorldSimSnapshot(
        ev.playerPos, ev.gameHour, rain01, snow01, windSpd, inInt, actors, nActors, nm, nv, nt,
        tickDtSec * 0.5f, g_coreWarmth);
    snap.simTicks = g_tickCounter / 2u;
    if (!inInt) snap.snowDepthU = std::max(snap.snowDepthU, g_snowAccumU);
    snap.totalSavesFound = g_saveCount;
    snap.largestSaveMb   = g_largestSaveMb;
    snap.saveBloatWarn   = g_saveBloatWarn ? 1u : 0u;
    // Live Wave 27 Synoptic Barometric Weather + Wave 34 Planetary Gravity/Tides + Extremum-Seeking GPU Governor:
    g_simClockSec += tickDtSec;
    const auto synLive = StepSynopticWeatherSim(ev.playerPos, g_simClockSec, ev.gameHour);
    snap.synopticHpa       = synLive.pressureHpa;
    snap.synopticCloud     = synLive.cloudCover01;
    snap.synopticRain      = synLive.rainRate01;
    snap.synopticSnow      = synLive.snowRate01;
    snap.synopticLightning = synLive.stormLightning;
    const auto planLive = EvaluatePlanetaryPhysics(ev.daysPassed, ev.gameHour, ev.playerPos,
                                                   std::max(windSpd * 0.014f, synLive.windSpeedMs), synLive.pressureHpa);
    snap.gravityMps2       = planLive.gravityMps2;
    snap.tideHeightU       = inInt ? 0.0f : planLive.tideHeightU;
    snap.tideVelocityMs    = inInt ? 0.0f : planLive.tideVelocityMs;
    snap.springTide01      = planLive.springTide01;
    snap.beaufortScale     = inInt ? 0.5f : planLive.beaufortScale;
    snap.swellDirX         = planLive.swellDirX;
    snap.swellDirY         = planLive.swellDirY;
    snap.simClockSec       = g_simClockSec;
    const float targetFrameSec = 1.0f / 120.0f;
    const float frameErr = (tickDtSec * 0.5f) - targetFrameSec;
    const float esCost = frameErr * frameErr * 10000.0f + (1.0f - g_gpuEsState.xHat) * 0.25f;
    snap.adaptiveQuality = StepExtremumSeekingGovernor(g_gpuEsState, esCost, tickDtSec, 0.75f, 1.0f, 0.015f, 8.0f, 0.35f);
    g_coreWarmth = snap.coreWarmth;
    g_shiverAtomic.store(snap.shiverIntensity, std::memory_order_relaxed);
    // Feed bounded live planetary gravity, tidal water buoyancy, and squad telemetry into Skyrim's live engine memory:
    WorldSimApplyLivePlanetaryGravity(snap.gravityMps2);
    ViewApplyLiveTide(snap.tideHeightU);
    GameplayHooksUpdateSquadSnapshot(snap.actorsTracked, snap.attackTokensUsed, snap.tacticalAction, storm, snap.snowDepthU);

    // Wave 40: Feed live weather, wind direction, hearth/fire proximity, water submersion & acoustics to rt_stealth_survival,
    // and Wave 42: Step regional 9-Hold EconomyLedger caravans/seasonal shocks + live NPC weather/night shelter & torch heuristics!
    {
        float hearthProx = 0.0f;
        EngineLight lights[32]{};
        uint64_t lFrame = 0;
        const uint32_t nLights = RAW_EngineLights(lights, 32u, &lFrame);
        for (uint32_t i = 0; i < nLights; ++i) {
            if ((lights[i].flags & 8u) == 0u) continue;  // kLightFire (campfire / brazier / forge / torch)
            const float dx = lights[i].pos[0] - ev.playerPos[0];
            const float dy = lights[i].pos[1] - ev.playerPos[1];
            const float dz = lights[i].pos[2] - ev.playerPos[2];
            const float d  = std::sqrt(dx * dx + dy * dy + dz * dz);
            const float reach = std::clamp(lights[i].radius * 0.85f, 220.0f, 520.0f);
            if (d < reach) {
                hearthProx = std::max(hearthProx, 1.0f - (d / reach));
            }
        }
        const float windMs = std::max(windSpd * 0.014f, synLive.windSpeedMs);
        const float wLen = std::sqrt(wx * wx + wy * wy);
        const bool nearHearth = (hearthProx > 0.12f);

        // Wave 42: Evaluate dynamic NPC weather/night shelter & torch heuristics and step live 9-Hold EconomyLedger!
        float cal6[6] = {201.0f, ev.gameMonth, ev.gameDay, ev.gameHour, ev.daysPassed, ev.timeScale};
        RAW_GetCalendarState(cal6);
        const SkyrimHold curHold = inInt ? SkyrimHold::Whiterun : ClassifySkyrimHold(ev.playerPos[0], ev.playerPos[1]);
        NpcEcosystemInput liveNpcIn{};
        liveNpcIn.gameHour            = ev.gameHour;
        liveNpcIn.rain01              = std::max(rain01, synLive.rainRate01 * 0.5f);
        liveNpcIn.snow01              = std::max(snow01, synLive.snowRate01 * 0.5f);
        liveNpcIn.storm01             = storm;
        liveNpcIn.snowDepthU          = snap.snowDepthU;
        liveNpcIn.windSpeedMs         = windMs;
        liveNpcIn.ambientTempC        = snap.ambientTempC;
        liveNpcIn.inInterior          = inInt;
        liveNpcIn.nearFire            = nearHearth;
        liveNpcIn.inCombat            = false;
        liveNpcIn.hold                = curHold;
        liveNpcIn.nearbyActors        = nActors;
        liveNpcIn.caravanDisruption01 = storm * 0.65f;
        NpcEcosystemDecision liveNpcDec = EvaluateNpcShelterTorchHeuristic(liveNpcIn);

        const HoldBarterMultipliers holdEcon = EconomyLedgerOnWorldTick(
            ev.playerPos, inInt, cal6[4], cal6[1], cal6[2],
            storm, liveNpcIn.rain01, snap.snowDepthU, liveNpcDec);

        // Re-evaluate alertness if hold caravan disruption exceeds raw local storm level:
        if (holdEcon.caravanDisruption01 > liveNpcIn.caravanDisruption01 + 0.05f) {
            liveNpcIn.hold = holdEcon.hold;
            liveNpcIn.caravanDisruption01 = holdEcon.caravanDisruption01;
            liveNpcDec = EvaluateNpcShelterTorchHeuristic(liveNpcIn);
        }
        snap.activeDirective = static_cast<uint8_t>(liveNpcDec.directive);
        snap.marketTier      = holdEcon.marketTier;
        snap.economyBuyMult  = holdEcon.buyPriceMult;
        // Feed live 9-Hold economy & thermal stamina multipliers into Skyrim's live GMSTs:
        const auto liveGm = ComputeDynamicGameplayGmsts(snap.economyBuyMult, snap.staminaRegenMult);
        WorldSimApplyLiveGmsts(liveGm.barterMin, liveGm.barterMax, liveGm.sprintStaminaDrainMult);

        StealthEnvSnapshot sEnv{};
        sEnv.rain01          = rain01;
        sEnv.snow01          = snow01;
        sEnv.storm01         = storm;
        sEnv.fog01           = std::clamp(0.12f + storm * 0.45f + ((ev.gameHour >= 4.5f && ev.gameHour <= 7.5f) ? 0.30f : 0.0f), 0.0f, 1.0f);
        sEnv.snowDepthU      = snap.snowDepthU;
        sEnv.ambientTempC    = snap.ambientTempC;
        sEnv.nightDarkness01 = (ev.gameHour < 5.0f || ev.gameHour > 21.0f) ? 0.85f :
                               ((ev.gameHour < 6.5f || ev.gameHour > 19.5f) ? 0.40f : 0.0f);
        sEnv.windDirX        = wLen > 1.0e-3f ? (wx / wLen) : 1.0f;
        sEnv.windDirY        = wLen > 1.0e-3f ? (wy / wLen) : 0.0f;
        sEnv.windSpeedMs     = windMs;

        const bool inWater = (!inInt && ev.waterHeight > -13999.0f && (ev.playerPos[2] + 30.0f) < ev.waterHeight);
        ThermalSurvivalInput tIn{};
        tIn.ambientTempC      = snap.ambientTempC;
        tIn.windSpeedMs       = windMs;
        tIn.rain01            = rain01;
        tIn.snow01            = snow01;
        tIn.altitudeZ         = ev.playerPos[2];
        tIn.inInterior        = inInt;
        tIn.inWater           = inWater;
        tIn.nearHearth        = nearHearth;
        tIn.hearthProximity01 = hearthProx;
        tIn.dtSec             = tickDtSec;

        AcousticClassificationInput aIn{};
        aIn.inInterior       = inInt;
        aIn.underwater       = inWater && ((ev.camPos[2] + 4.0f) < ev.waterHeight);
        aIn.isIceCavern      = inInt && (ev.playerPos[1] > 24000.0f);
        aIn.altitudeZ        = ev.playerPos[2];
        aIn.worldY           = ev.playerPos[1];
        aIn.snowCover01      = snow01;
        aIn.rain01           = rain01;
        aIn.windSpeedMs      = windMs;
        aIn.temperatureC     = snap.ambientTempC;
        aIn.humidity01       = std::clamp(0.45f + rain01 * 0.45f + snow01 * 0.25f, 0.15f, 0.98f);
        aIn.enclosureRadiusU = inInt ? 720.0f : 4000.0f;
        StealthSurvivalUpdateFrame(sEnv, tIn, aIn);
    }

    std::lock_guard<std::mutex> lk(g_simMu);
    g_tel = snap;
}

void WorldSimShutdown() {
    g_shutdown.store(true, std::memory_order_release);
    ViewRestoreAllTides();
    WorldSimRestoreDynamicSettings();
}
#endif

}  // namespace raw::rt

extern "C" __declspec(dllexport) int RAW_GetWorldSimTelemetry(raw::rt::RawWorldSimTelemetry* out) {
    if (!out) return 0;
    std::lock_guard<std::mutex> lk(raw::rt::g_simMu);
    *out = raw::rt::g_tel;
    return 1;
}
