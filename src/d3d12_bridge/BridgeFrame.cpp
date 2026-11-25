// D3D12Bridge per-frame work: depth capture, the fence round trip, and GPU
// timestamps on both queues. Original work, MIT.
#include "BridgeImpl.h"
#include "RawProceduralLib.h"
#include "StreamlineHost.h"
#include "LightingPass.h"
#include "RtScene.h"
#include "../worldgen/RawHydrologyGen.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>

namespace raw::bridge
{
namespace
{

// Exact byte-for-byte mirror of LightingPass.cpp LightUniform (cbuffer P) for zero-coupling telemetry readback.
struct TelemetryLightUniformView
{
    float dims[4], proj[4], clipToWorld[16], worldToClip[16], viewToWorld[12];
    float resScale[4], sunDir[4], sunColour[4], ambient[12], cascade[24], cascadeInfo[4];
    uint32_t flags[4];
    float cluster[4], tune[4], tune2[4], weather[4], camPos[4], clouds[4], cloudWind[4], waterA[4], waterB[4], prevWorldToClip[16];
    float lightPosR[kMaxSceneLights][4], lightColS[kMaxSceneLights][4];
    uint32_t clusters[kClusterCount][4];
    float footTrail[32][4];
    float dofParams[4];
    float timeEnv[4];
    float engineWorld[4];
    float camOptics[2][4];
    float physWorld[2][4];
    float seasonThermal0[4];      // x: snowDepthM, y: snowAlbedo, z: snowCompaction01, w: surfaceTempC
    float seasonThermal1[4];      // x: iceThicknessM, y: blackIce01, z: frostRime01, w: iceFracture01
    float phenology0[4];
    float phenology1[4];
    float impactDecalPosR[16][4]; // xyz: world pos, w: radiusU (0 = inactive)
    float impactDecalMeta[16][4];
};
static_assert(sizeof(TelemetryLightUniformView) == 40 * 16 + 2 * kMaxSceneLights * 16 + kClusterCount * 16 + 32 * 16 + 176 + 512,
              "TelemetryLightUniformView must match LightingPass cbuffer P");

} // namespace

void D3D12Bridge::Impl::EmitLiveTelemetry(bool rtActive)
{
    if (frames != 1ull && (frames % 180ull) != 0ull) return;

    float seasonSnowM = 0.0f;
    float seasonIceM = 0.0f;
    float skinTempC = 0.0f;
    uint32_t activeImpactDecals = 0u;

    const void* uPtr = nullptr;
    size_t uBytes = 0;
    if (pass && LightingPassUniform(pass.get(), &uPtr, &uBytes) &&
        uPtr != nullptr && uBytes >= sizeof(TelemetryLightUniformView)) {
        const auto* u = static_cast<const TelemetryLightUniformView*>(uPtr);
        seasonSnowM = std::isfinite(u->seasonThermal0[0]) ? u->seasonThermal0[0] : 0.0f;
        skinTempC   = std::isfinite(u->seasonThermal0[3]) ? u->seasonThermal0[3] : 0.0f;
        seasonIceM  = std::isfinite(u->seasonThermal1[0]) ? u->seasonThermal1[0] : 0.0f;
        for (uint32_t i = 0; i < 16u; ++i) {
            if (u->impactDecalPosR[i][3] > 0.0f) ++activeImpactDecals;
        }
    }

    uint32_t gpuMeshlets = 0u;
    for (const auto& kv : streamedTerrainGpu) {
        gpuMeshlets += kv.second.meshletCount;
    }
    const uint32_t worldGenMeshlets = (gpuMeshlets > 0u) ? gpuMeshlets : lastWorldStats.totalMeshlets;
    const uint32_t worldGenActiveChunks = lastWorldStats.activeChunkCount;
    const uint32_t worldGenPendingAsync = worldShowcase.PendingAsyncJobs();
    const char* biomeName = raw::worldgen::GetBiomeName(lastWorldStats.cameraBiome);
    if (!biomeName) biomeName = "Unknown";

    const FluidGpuTimings fluidTimings = fluidReady ? fluidSim.GetLastTimings() : FluidGpuTimings{};
    const RadianceGpuTimings radTimings = radianceReady ? radianceHost.GetLastTimings() : RadianceGpuTimings{};
    const float fluidFireMs = (fluidTimings.valid && std::isfinite(fluidTimings.fireStepMs)) ? fluidTimings.fireStepMs : 0.0f;
    const float fluidWaterMs = (fluidTimings.valid && std::isfinite(fluidTimings.waterStepMs)) ? fluidTimings.waterStepMs : 0.0f;
    const uint32_t fluidPressureIters = fluidTimings.activePressureIters;
    const float radianceGiMs = (radTimings.valid && std::isfinite(radTimings.giPassMs)) ? radTimings.giPassMs : 0.0f;
    const float xpbdStepMs = (xpbdSimReady && xpbdSim && xpbdSim->HasValidTiming() && std::isfinite(xpbdSim->GetLastStepMs()))
        ? xpbdSim->GetLastStepMs() : 0.0f;
    const uint32_t xpbdParticles = (xpbdSimReady && xpbdSim) ? xpbdSim->ParticleCount() : 0u;

    char jsonBuf[2048]{};
    const int n = std::snprintf(
        jsonBuf, sizeof(jsonBuf),
        "{\n"
        "  \"frame\": %llu,\n"
        "  \"width\": %u,\n"
        "  \"height\": %u,\n"
        "  \"attached\": %s,\n"
        "  \"rtActive\": %s,\n"
        "  \"worldGenActiveChunks\": %u,\n"
        "  \"worldGenMeshlets\": %u,\n"
        "  \"worldGenPendingAsync\": %u,\n"
        "  \"worldGenBiome\": \"%s\",\n"
        "  \"fluidFireEmitters\": %u,\n"
        "  \"fluidWaterWakes\": %u,\n"
        "  \"fluidFireMs\": %.4f,\n"
        "  \"fluidWaterMs\": %.4f,\n"
        "  \"fluidPressureIters\": %u,\n"
        "  \"radianceGiMs\": %.4f,\n"
        "  \"xpbdStepMs\": %.4f,\n"
        "  \"xpbdParticles\": %u,\n"
        "  \"seasonSnowM\": %.4f,\n"
        "  \"seasonIceM\": %.4f,\n"
        "  \"skinTempC\": %.3f,\n"
        "  \"activeImpactDecals\": %u,\n"
        "  \"radianceCascadesActive\": %s,\n"
        "  \"hydrologyErosionActive\": %s\n"
        "}\n",
        static_cast<unsigned long long>(frames),
        static_cast<unsigned>(cfg.width),
        static_cast<unsigned>(cfg.height),
        attached ? "true" : "false",
        rtActive ? "true" : "false",
        worldGenActiveChunks,
        worldGenMeshlets,
        worldGenPendingAsync,
        biomeName,
        lastFluidFireEmitters,
        lastFluidWaterWakes,
        fluidFireMs,
        fluidWaterMs,
        fluidPressureIters,
        radianceGiMs,
        xpbdStepMs,
        xpbdParticles,
        seasonSnowM,
        seasonIceM,
        skinTempC,
        activeImpactDecals,
        (radianceReady && radianceHost.ScreenGIReady()) ? "true" : "false",
        raw::worldgen::IsTerrainGenHydrologyErosionEnabled() ? "true" : "false");
    if (n <= 0) return;

    try {
        const std::string outDir = telemetryOutDir.empty() ? std::string("raw_engine_out") : telemetryOutDir;
        CreateDirectoryA(outDir.c_str(), nullptr);
        const std::string tmpPath = outDir + "\\bridge_live_telemetry.json.tmp";
        const std::string dstPath = outDir + "\\bridge_live_telemetry.json";
        std::ofstream ofs(tmpPath, std::ios::binary | std::ios::trunc);
        if (ofs.is_open()) {
            ofs.write(jsonBuf, static_cast<std::streamsize>(n));
            ofs.flush();
            const bool writeOk = ofs.good();
            ofs.close();
            if (writeOk) {
                MoveFileExA(tmpPath.c_str(), dstPath.c_str(), MOVEFILE_REPLACE_EXISTING);
            }
        }
    } catch (...) {
        // Non-blocking telemetry: I/O failures must never impact frame rendering.
    }
}

bool D3D12Bridge::Impl::StepFluidSimOnQueue(bool isInterior)
{
    if (!fluidReady || !fluidSim.Ready() || !queue || !fluidList || !fluidFence || !fluidEvent) return false;

    const uint32_t slot = fluidSlot;
    if (fluidSlotVal[slot] > 0 && fluidFence->GetCompletedValue() < fluidSlotVal[slot]) {
        fluidFence->SetEventOnCompletion(fluidSlotVal[slot], fluidEvent);
        WaitForSingleObject(fluidEvent, 3000);
    }
    if (FAILED(fluidAlloc[slot]->Reset()) || FAILED(fluidList->Reset(fluidAlloc[slot].Get(), nullptr))) {
        return false;
    }

    const uint64_t nowMs = GetTickCount64();
    float dtSec = 1.0f / 60.0f;
    if (fluidLastMs != 0) {
        dtSec = std::clamp(float(double(nowMs - fluidLastMs) * 0.001), 1.0e-4f, 0.05f);
    }
    fluidLastMs = nowMs;
    if (lighting.menuPaused) dtSec = 1.0e-4f;

    LightingFluidInputs fi{};
    LightingPassFluidInputs(pass.get(), &fi);

    // 1. Build 64^3 3D Eulerian Navier-Stokes Fire/Smoke params from verified warm lights + live wind:
    FluidFireParams fp{};
    fp.cellSize = 4.0f; // 64 * 4 = 256u domain cube
    fp.dt = dtSec;
    fp.timeSec = fi.simTimeSec;
    fp.windVelW[0] = isInterior ? 0.0f : std::clamp(fi.windXY[0] * 0.12f, -36.0f, 36.0f);
    fp.windVelW[1] = isInterior ? 0.0f : std::clamp(fi.windXY[1] * 0.12f, -36.0f, 36.0f);
    fp.windVelW[2] = 0.0f;
    fp.ambientTempK = std::clamp(fi.ambientTempC + 273.15f, 220.0f, 330.0f);
    fp.enableCeiling = isInterior;
    fp.jacobiIterations = 8u;

    // Anchor domain around closest strictly-verified warm fire emitter within 1400u (falls back to camera pos):
    float anchorW[3] = {lighting.posAdjust[0], lighting.posAdjust[1], lighting.posAdjust[2]};
    float bestFireD2 = 1400.0f * 1400.0f;
    float minEmitterZ = lighting.posAdjust[2] - 110.0f;
    uint32_t emitCount = 0u;
    for (uint32_t i = 0; i < lighting.lightCount && emitCount < 16u; ++i) {
        const SceneLight& l = lighting.lights[i];
        // Strict fire-core chromaticity + physical hearth/torch/brazier radius gate (never wide neutral room-fill lights!):
        if (l.colour[0] > l.colour[1] * 1.25f && l.colour[0] > l.colour[2] * 2.2f &&
            l.colour[0] > 0.45f && l.radius >= 50.0f && l.radius <= 900.0f) {
            const float wx = lighting.posAdjust[0] + l.position[0];
            const float wy = lighting.posAdjust[1] + l.position[1];
            const float wz = lighting.posAdjust[2] + l.position[2];
            const float d2 = l.position[0] * l.position[0] + l.position[1] * l.position[1] + l.position[2] * l.position[2];
            if (d2 < bestFireD2) {
                bestFireD2 = d2;
                anchorW[0] = wx; anchorW[1] = wy; anchorW[2] = wz - 32.0f;
            }
            const float emitZ = wz - 20.0f;
            minEmitterZ = (emitCount == 0u) ? emitZ : std::min(minEmitterZ, emitZ);
            FluidFireEmitter& em = fp.emitters[emitCount++];
            em.posW[0] = wx;
            em.posW[1] = wy;
            em.posW[2] = emitZ;
            em.radius = std::clamp(l.radius * 0.08f, 6.0f, 26.0f);
            em.ignitionTempK = std::clamp(1950.0f + l.colour[0] * 900.0f, 1950.0f, 3300.0f);
            em.fuelRate = 2.4f;
            em.jetSpeed = 30.0f;
            em.curlStrength = 14.0f;
            em.jetDir[0] = 0.0f; em.jetDir[1] = 0.0f; em.jetDir[2] = 1.0f;
            em.sootSeedRate = 0.35f;
        }
    }
    fp.emitterCount = emitCount;
    const float domainSpan = float(RawFluidSimHost::kFireGridRes) * fp.cellSize;
    const float halfDomain = 0.5f * domainSpan;
    fp.domainOriginW[0] = anchorW[0] - halfDomain;
    fp.domainOriginW[1] = anchorW[1] - halfDomain;
    const float floorZ = (emitCount > 0u) ? (minEmitterZ - 14.0f) : (lighting.posAdjust[2] - 124.0f);
    fp.domainOriginW[2] = floorZ - fp.cellSize * 2.0f;
    fp.floorZLocal = std::clamp((floorZ - fp.domainOriginW[2]) / fp.cellSize, 1.0f, 10.0f);

    // Inject real physical floor slab + interior ceiling baffle AABBs so 3D NS fire & smoke collide and spread across ceilings:
    fp.obstacles[0].minW[0] = fp.domainOriginW[0] - 64.0f;
    fp.obstacles[0].minW[1] = fp.domainOriginW[1] - 64.0f;
    fp.obstacles[0].minW[2] = floorZ - 24.0f;
    fp.obstacles[0].maxW[0] = fp.domainOriginW[0] + domainSpan + 64.0f;
    fp.obstacles[0].maxW[1] = fp.domainOriginW[1] + domainSpan + 64.0f;
    fp.obstacles[0].maxW[2] = floorZ;
    fp.obstacles[0].active = true;
    fp.obstacles[0].roughness = 0.65f;
    fp.obstacleCount = 1u;

    if (isInterior) {
        const float ceilZ = floorZ + 240.0f;
        fp.ceilingZLocal = std::clamp((ceilZ - fp.domainOriginW[2]) / fp.cellSize, 42.0f, 62.0f);
        fp.obstacles[1].minW[0] = fp.domainOriginW[0] - 64.0f;
        fp.obstacles[1].minW[1] = fp.domainOriginW[1] - 64.0f;
        fp.obstacles[1].minW[2] = ceilZ;
        fp.obstacles[1].maxW[0] = fp.domainOriginW[0] + domainSpan + 64.0f;
        fp.obstacles[1].maxW[1] = fp.domainOriginW[1] + domainSpan + 64.0f;
        fp.obstacles[1].maxW[2] = ceilZ + 32.0f;
        fp.obstacles[1].active = true;
        fp.obstacles[1].roughness = 0.55f;
        fp.obstacleCount = 2u;
    }

    // 2. Build 256^2 Toroidal Shallow-Water Hydrodynamics params from camPos, footTrail[0..31], rain & Beaufort swell:
    FluidWaterParams wp{};
    wp.camPosXY[0] = lighting.posAdjust[0];
    wp.camPosXY[1] = lighting.posAdjust[1];
    wp.cellSize = 8.0f; // 256 * 8 = 2048u toroidal span
    wp.dt = dtSec;
    wp.timeSec = fi.simTimeSec;
    wp.windDirXY[0] = fi.swellDirXY[0];
    wp.windDirXY[1] = fi.swellDirXY[1];
    wp.swellAmp = isInterior ? 0.15f : std::clamp(0.45f + fi.beaufortScale * 0.42f, 0.25f, 4.5f);
    wp.rainIntensity = isInterior ? 0.0f : std::clamp(fi.rainIntensity, 0.0f, 1.0f);
    wp.waterPlaneZ = fi.waterPlaneZ;
    uint32_t wakeCount = 0u;
    for (uint32_t i = 0; i < 32u; ++i) {
        if (fi.footTrail[i][3] > 0.01f) {
            FluidWaterTrail& tr = wp.trails[wakeCount++];
            tr.posW[0] = fi.footTrail[i][0];
            tr.posW[1] = fi.footTrail[i][1];
            tr.posW[2] = fi.footTrail[i][2];
            tr.strength = fi.footTrail[i][3];
            tr.velW[0] = 0.0f;
            tr.velW[1] = 0.0f;
            tr.radius = 20.0f;
            tr.splashScale = 1.0f;
        }
    }
    wp.trailCount = wakeCount;
    lastFluidFireEmitters = emitCount;
    lastFluidWaterWakes = wakeCount;

    if (emitCount > 0u || fluidStepFrames == 0ull) {
        fluidSim.StepFire(fluidList.Get(), fp);
    }
    fluidSim.StepWater(fluidList.Get(), wp);

    if (FAILED(fluidList->Close())) return false;
    ID3D12CommandList* lists[1] = {fluidList.Get()};
    queue->ExecuteCommandLists(1, lists);
    queue->Signal(fluidFence.Get(), ++fluidFenceVal);
    fluidSlotVal[slot] = fluidFenceVal;
    fluidSlot = (slot + 1u) % kRing;

    if ((fluidStepFrames++ % 180ull) == 0ull) {
        log("[FluidSim] live GPU step: fireEmitters=%u, waterWakes=%u", emitCount, wakeCount);
    }
    return true;
}

bool D3D12Bridge::Impl::ExecuteRadianceScreenGiOnQueue()
{
    if (!radianceReady || !radianceHost.ScreenGIReady() || !queue || !radList || !radFence || !radEvent ||
        !colorOut.res12 || !depthIn.res12 || cfg.width == 0u || cfg.height == 0u) {
        return false;
    }
    if ((colorOut.res12->GetDesc().Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) == 0) {
        return false;
    }

    const uint32_t slot = radSlot;
    if (radSlotVal[slot] > 0 && radFence->GetCompletedValue() < radSlotVal[slot]) {
        radFence->SetEventOnCompletion(radSlotVal[slot], radEvent);
        WaitForSingleObject(radEvent, 3000);
    }
    if (FAILED(radAlloc[slot]->Reset()) || FAILED(radList->Reset(radAlloc[slot].Get(), nullptr))) {
        return false;
    }

    radianceHost.ExecuteScreenGI(radList.Get(), colorOut.res12.Get(), depthIn.res12.Get(),
                                 cfg.width, cfg.height, 0.16f);

    if (FAILED(radList->Close())) return false;
    ID3D12CommandList* lists[1] = {radList.Get()};
    queue->ExecuteCommandLists(1, lists);
    queue->Signal(radFence.Get(), ++radFenceVal);
    radSlotVal[slot] = radFenceVal;
    radSlot = (slot + 1u) % kRing;

    if ((radStepFrames++ % 180ull) == 0ull) {
        log("[RadianceCascades] live 4-cascade ScreenGI active (%ux%u, strength=0.16)",
            static_cast<unsigned>(cfg.width), static_cast<unsigned>(cfg.height));
    }
    return true;
}

bool D3D12Bridge::Impl::StepXpbdSimOnQueue(bool isInterior)
{
    if (!xpbdSimReady || !xpbdSim || !xpbdSim->Ready() || !queue || !xpbdList || !xpbdFence || !xpbdEvent) {
        return false;
    }

    const uint32_t slot = xpbdSlot;
    if (xpbdSlotVal[slot] > 0 && xpbdFence->GetCompletedValue() < xpbdSlotVal[slot]) {
        xpbdFence->SetEventOnCompletion(xpbdSlotVal[slot], xpbdEvent);
        WaitForSingleObject(xpbdEvent, 3000);
    }
    if (FAILED(xpbdAlloc[slot]->Reset()) || FAILED(xpbdList->Reset(xpbdAlloc[slot].Get(), nullptr))) {
        return false;
    }

    if (!xpbdChainsUploaded) {
        std::vector<XpbdParticle> particles;
        std::vector<XpbdChainMeta> chains;
        BuildHairAndFoliageDemoChains(particles, chains, 256u);
        if (!xpbdSim->UploadChains(xpbdList.Get(), particles, chains)) {
            xpbdList->Close();
            return false;
        }
        xpbdChainsUploaded = true;
    }

    const uint64_t nowMs = GetTickCount64();
    float dtSec = 1.0f / 60.0f;
    if (xpbdLastMs != 0) {
        dtSec = std::clamp(float(double(nowMs - xpbdLastMs) * 0.001), 1.0e-4f, 0.05f);
    }
    xpbdLastMs = nowMs;
    if (lighting.menuPaused) dtSec = 1.0e-4f;

    LightingFluidInputs fi{};
    LightingPassFluidInputs(pass.get(), &fi);

    float seasonSnowM = 0.0f;
    const void* uPtr = nullptr;
    size_t uBytes = 0;
    if (pass && LightingPassUniform(pass.get(), &uPtr, &uBytes) &&
        uPtr != nullptr && uBytes >= sizeof(TelemetryLightUniformView)) {
        const auto* u = static_cast<const TelemetryLightUniformView*>(uPtr);
        if (std::isfinite(u->seasonThermal0[0])) {
            seasonSnowM = std::max(0.0f, u->seasonThermal0[0]);
        }
    }

    XpbdStepParams xp{};
    xp.dt              = dtSec;
    xp.timeSec         = fi.simTimeSec;
    xp.substeps        = 4u;
    xp.gravityZ        = -9.80665f;
    const float wLen   = std::sqrt(fi.windXY[0] * fi.windXY[0] + fi.windXY[1] * fi.windXY[1]);
    if (wLen > 1.0e-4f) {
        xp.windDir[0] = fi.windXY[0] / wLen;
        xp.windDir[1] = fi.windXY[1] / wLen;
        xp.windDir[2] = 0.0f;
    } else {
        xp.windDir[0] = 0.92f;
        xp.windDir[1] = 0.39f;
        xp.windDir[2] = 0.0f;
    }
    xp.windSpeedMps    = isInterior ? 0.35f : std::clamp(0.6f + fi.beaufortScale * 2.2f, 0.5f, 24.0f);
    xp.gustPhase       = fi.simTimeSec * 0.85f;
    xp.snowLoad01      = isInterior ? 0.0f : std::clamp(seasonSnowM * 1.5f, 0.0f, 1.0f);
    xp.maxSpeed        = 35.0f;
    xp.collisionMargin = 0.005f;

    // Head sphere + torso capsule + animated bush push-aside wake sphere:
    xp.colliders[0].p0[0]  = 0.0f; xp.colliders[0].p0[1] = 0.0f; xp.colliders[0].p0[2] = 1.62f;
    xp.colliders[0].radius = 0.14f;
    xp.colliders[0].flags  = 0.0f;

    xp.colliders[1].p0[0]  = 0.0f; xp.colliders[1].p0[1] = 0.0f; xp.colliders[1].p0[2] = 1.48f;
    xp.colliders[1].p1[0]  = 0.0f; xp.colliders[1].p1[1] = 0.0f; xp.colliders[1].p1[2] = 0.88f;
    xp.colliders[1].radius = 0.22f;
    xp.colliders[1].flags  = 1.0f;

    xp.colliders[2].p0[0]  = 3.0f + std::sin(fi.simTimeSec * 0.7f) * 0.35f;
    xp.colliders[2].p0[1]  = std::cos(fi.simTimeSec * 0.5f) * 0.35f;
    xp.colliders[2].p0[2]  = 0.65f;
    xp.colliders[2].radius = 0.32f;
    xp.colliders[2].flags  = 0.0f;
    xp.colliderCount       = 3u;

    xpbdSim->Step(xpbdList.Get(), xp);

    if (FAILED(xpbdList->Close())) return false;
    ID3D12CommandList* lists[1] = {xpbdList.Get()};
    queue->ExecuteCommandLists(1, lists);
    queue->Signal(xpbdFence.Get(), ++xpbdFenceVal);
    xpbdSlotVal[slot] = xpbdFenceVal;
    xpbdSlot = (slot + 1u) % kRing;

    if ((xpbdStepFrames++ % 180ull) == 0ull) {
        log("[XpbdSim] live GPU step: chains=%u, particles=%u, stepMs=%.4f, wind=%.1f m/s, snowLoad=%.2f",
            xpbdSim->ChainCount(), xpbdSim->ParticleCount(),
            xpbdSim->HasValidTiming() ? xpbdSim->GetLastStepMs() : 0.0f,
            xp.windSpeedMps, xp.snowLoad01);
    }
    return true;
}

bool D3D12Bridge::Impl::CpuWait(uint64_t value, DWORD timeoutMs)
{
    return WaitFence(fence12.Get(), value, cpuEvent, timeoutMs);
}

void D3D12Bridge::Impl::OpenD11Slot(D11Slot& s, uint64_t frame)
{
    if (s.open && s.frame == frame) return;
    if (s.open) ctx11->End(s.disjoint.Get());   // stale, never executed
    ctx11->Begin(s.disjoint.Get());
    s.open = true;
    s.frame = frame;
    s.hasDepth = false;
    s.hasNormal = false;
    s.hasMotion = false;
    s.hasSurface = false;
    s.hasShadows = false;
    s.executed = false;
}

bool D3D12Bridge::CaptureDepth(ID3D11ShaderResourceView* depthSrv)
{
    if (!m->ready || !depthSrv) return false;
    ComPtr<ID3D11Resource> res;
    ComPtr<ID3D11Texture2D> tex;
    depthSrv->GetResource(&res);
    if (!res || FAILED(res.As(&tex))) return false;
    D3D11_TEXTURE2D_DESC d{};
    tex->GetDesc(&d);
    if (d.Width != m->cfg.width || d.Height != m->cfg.height || d.SampleDesc.Count != 1)
        return false;

    auto* c = m->ctx11.Get();
    // Save the compute-stage bindings this pass overwrites.
    ComPtr<ID3D11ComputeShader> oldCS;
    ComPtr<ID3D11ShaderResourceView> oldSRV;
    ComPtr<ID3D11UnorderedAccessView> oldUAV;
    UINT numInst = 0;
    c->CSGetShader(&oldCS, nullptr, &numInst);
    c->CSGetShaderResources(0, 1, &oldSRV);
    c->CSGetUnorderedAccessViews(0, 1, &oldUAV);

    D11Slot& s = m->SlotFor(m->frames);
    m->OpenD11Slot(s, m->frames);
    c->End(s.ts[0].Get());
    ID3D11UnorderedAccessView* uav = m->depthUAV.Get();
    c->CSSetShader(m->depthCS.Get(), nullptr, 0);
    c->CSSetShaderResources(0, 1, &depthSrv);
    c->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
    c->Dispatch((d.Width + 7) / 8, (d.Height + 7) / 8, 1);
    c->End(s.ts[1].Get());

    ID3D11ShaderResourceView* srv0 = oldSRV.Get();
    ID3D11UnorderedAccessView* uav0 = oldUAV.Get();
    const UINT keepCount = ~0u;
    c->CSSetUnorderedAccessViews(0, 1, &uav0, &keepCount);
    c->CSSetShaderResources(0, 1, &srv0);
    c->CSSetShader(oldCS.Get(), nullptr, 0);
    s.hasDepth = true;
    return true;
}

bool D3D12Bridge::CaptureNormal(ID3D11Texture2D* normalTex)
{
    if (!m->ready || !normalTex || !m->normalIn.tex11) return false;
    D3D11_TEXTURE2D_DESC d{};
    normalTex->GetDesc(&d);
    if (d.Width != m->cfg.width || d.Height != m->cfg.height || d.SampleDesc.Count != 1) return false;
    D11Slot& s = m->SlotFor(m->frames);
    m->OpenD11Slot(s, m->frames);
    auto* c = m->ctx11.Get();
    c->End(s.ts[2].Get());
    c->CopyResource(m->normalIn.tex11.Get(), normalTex);
    if (m->pendingMotion) {
        c->CopyResource(m->motionIn.tex11.Get(), m->pendingMotion);
        m->pendingMotion = nullptr;
        s.hasMotion = true;
    }
    c->End(s.ts[3].Get());
    s.hasNormal = true;
    return true;
}

bool D3D12Bridge::CaptureMotion(ID3D11Texture2D* motionTex)
{
    if (!m->ready || !motionTex || !m->motionIn.tex11) return false;
    D3D11_TEXTURE2D_DESC d{};
    motionTex->GetDesc(&d);
    if (d.Width != m->cfg.width || d.Height != m->cfg.height || d.SampleDesc.Count != 1 || d.Format != m->cfg.motionFormat) return false;
    m->pendingMotion = motionTex;
    return true;
}

void D3D12Bridge::SetCamera(const CameraConstants& camera) { m->camera = camera; }

void D3D12Bridge::SetRtDraws(std::vector<RtDraw>&& draws)
{
    for (RtDraw& d : m->pendingDraws) ReleaseRtDraw(d);
    m->pendingDraws = std::move(draws);
    if (!m->rt) {
        for (RtDraw& d : m->pendingDraws) ReleaseRtDraw(d);
        m->pendingDraws.clear();
    }
}
void D3D12Bridge::TuneRt(const RtTuning& t) { m->rtTune = t; if (m->rt) m->rt->Tune(t); }
bool D3D12Bridge::ReloadRt(const std::vector<uint8_t>& shadow, const std::vector<uint8_t>& sky, std::string& err)
{
    if (!m->rt) { err = "no ray-tracing scene (RtShadows=0, or not on raw::tx)"; return false; }
    return m->rt->Reload(shadow, sky, err);
}

bool D3D12Bridge::RtStats(RtFrameStats* out)
{
    if (!m->rt || !out) return false;
    *out = m->rt->Stats();
    return true;
}

// Stores the pass's newest retired timings under the frame they belong to.
void D3D12Bridge::Impl::HarvestPassTimings()
{
    PassTimings pt;
    if (!pass || !pass->LastTimings(&pt)) return;
    D12Slot& ds = d12[pt.submission % kRing];
    ds.frameTag = pt.submission;
    ds.pass = pt;
    ds.havePass = true;
}

bool D3D12Bridge::Execute(ID3D11Resource* colorSrc, ID3D11Resource* colorDst,
                          const PassConstants& kIn)
{
    if (!m->ready || !colorSrc || !colorDst) return false;
    const uint64_t frame = m->frames;

    // Fence values: D3D11 signals v1, the D3D12 queue waits for v1 + skew,
    // D3D12 signals v2, D3D11 waits for v2. Values only ever increase.
    const uint64_t v1 = m->nextValue;
    const uint64_t waitValue = v1 + m->skew;
    const uint64_t v2 = waitValue + 1;

    D11Slot& s = m->SlotFor(frame);
    m->OpenD11Slot(s, frame);
    FrameConstants fc;
    fc.k = kIn;
    fc.k.dims[0] = static_cast<float>(m->cfg.width);
    fc.k.dims[1] = static_cast<float>(m->cfg.height);
    fc.k.dims[2] = 1.0f / fc.k.dims[0];
    fc.k.dims[3] = 1.0f / fc.k.dims[1];
    fc.camera = m->camera;
    fc.hasNormal = s.hasNormal;
    fc.hasMotion = s.hasMotion;
    fc.rt = m->attached && m->rt && m->rt->Active() && m->cfg.lightingPass;
    if (m->cfg.lightingPass) {
        fc.lighting = &m->lighting;
        fc.hasSurface = s.hasSurface;
        fc.hasShadows = s.hasShadows;
        // Live beyond-Tamriel 6-biome frontier chunk + 3D procedural hero prop streaming (O(1) no-op inside Tamriel):
        if (m->lighting.valid && m->meshHost.TerrainReady()) {
            struct BridgeChunkSink final : raw::worldgen::IGpuChunkSink {
                D3D12Bridge::Impl* impl = nullptr;
                uint64_t OnChunkLoaded(const raw::worldgen::StreamedChunkEntry& entry) override {
                    raw::worldgen::ProceduralMesh merged{};
                    const auto& wcfg = impl->worldShowcase.Config();
                    if (!raw::worldgen::BuildChunkMergedGpuMesh(entry, wcfg.maxMeshletTris, wcfg.terrain.seed,
                                                                wcfg.terrain.seaLevelZ, &merged)) {
                        return 0u;
                    }
                    TerrainGpuChunk gpu{};
                    if (!impl->meshHost.UploadTerrainChunk(merged, &gpu)) return 0u;
                    const uint64_t h = impl->nextTerrainHandle++;
                    impl->streamedTerrainGpu.emplace(h, std::move(gpu));
                    return h;
                }
                void OnChunkEvicted(raw::worldgen::ChunkKey, uint64_t gpuHandle) override {
                    if (gpuHandle == 0u) return;
                    auto it = impl->streamedTerrainGpu.find(gpuHandle);
                    if (it != impl->streamedTerrainGpu.end()) {
                        impl->meshHost.DeferRelease(std::move(it->second));
                        impl->streamedTerrainGpu.erase(it);
                    }
                }
            };
            BridgeChunkSink sink;
            sink.impl = m.get();
            m->lastWorldStats = m->worldShowcase.Update(
                {m->lighting.posAdjust[0], m->lighting.posAdjust[1], m->lighting.posAdjust[2]}, &sink);
        }
    }

    // The pass records first, so recording time never shows up as a stall,
    // and submits only after the D3D11 Signal: in the game process D3D11
    // rejects Signal(v1) with E_INVALIDARG once D3D12 has a pending Signal(v2).
    if (!m->pass->Record(fc)) {
        // The frame before may still have a D3D11 Wait queued for a value the
        // pass will now never signal. Signal it from the CPU, or the game's
        // D3D11 queue stays blocked for good.
        m->log("[D3D12Bridge] pass '%s' failed to record; releasing fence value %llu and turning the bridge off",
               m->pass->Name(), (unsigned long long)m->lastD12);
        if (m->dev12 && FAILED(m->dev12->GetDeviceRemovedReason())) LogDeviceRemoved(m->dev12.Get(), m->log);
        if (m->fence12 && m->fence12->GetCompletedValue() < m->lastD12) m->fence12->Signal(m->lastD12);
        m->ready = false;
        return false;
    }
    m->HarvestPassTimings();   // recording may have retired the frame before
    m->nextValue = v2 + 1;

    // Determine whether live RawTerrainWorld.hlsl multi-biome rendering runs after LightingPass this frame.
    const bool isInterior = m->lighting.hasEngineCell
        ? m->lighting.isInterior
        : !(s.hasShadows && m->lighting.hasCascades);
    const void* terrainU = nullptr;
    size_t terrainUb = 0;
    const bool runTerrain = m->cfg.lightingPass && m->lighting.valid && !isInterior &&
                            !m->streamedTerrainGpu.empty() && m->meshHost.TerrainReady() &&
                            LightingPassUniform(m->pass.get(), &terrainU, &terrainUb);
    const bool runFluid = m->cfg.lightingPass && m->lighting.valid && m->fluidReady && m->fluidSim.Ready();
    const bool runRadiance = m->cfg.lightingPass && m->lighting.valid && m->radianceReady && m->radianceHost.ScreenGIReady();
    const bool runXpbd = m->cfg.lightingPass && m->lighting.valid && m->xpbdSimReady && m->xpbdSim && m->xpbdSim->Ready();
    const bool hasPostWork = runTerrain || runFluid || runRadiance || runXpbd;
    auto runPostLightingPasses = [&]() -> bool {
        if (!hasPostWork) return true;
        if (runTerrain) {
            uint32_t drawnChunks = 0u, drawnMeshlets = 0u;
            const float timeSec = float(double(GetTickCount64() % 3600000ull) / 1000.0);
            ID3D12Resource* atlasRes = s.hasShadows ? m->shared[static_cast<UINT>(Shared::ShadowAtlas)].res12.Get() : nullptr;
            const bool okT = m->meshHost.ExecuteTerrainPass(
                m->queue.Get(), m->colorOut.res12.Get(), m->depthIn.res12.Get(), atlasRes,
                m->cfg.width, m->cfg.height, terrainU, terrainUb,
                &m->lighting.worldToClip[0][0], m->lighting.posAdjust, timeSec,
                m->streamedTerrainGpu, &drawnChunks, &drawnMeshlets);
            if (okT) {
                if ((m->terrainRenderFrames++ % 180ull) == 0ull) {
                    m->log("[WorldGen] LIVE render: %u chunks (%u meshlets) at cam (%.0f, %.0f, %.0f) biome=%s",
                           drawnChunks, drawnMeshlets,
                           m->lighting.posAdjust[0], m->lighting.posAdjust[1], m->lighting.posAdjust[2],
                           raw::worldgen::GetBiomeName(m->lastWorldStats.cameraBiome));
                }
            }
        }
        if (runFluid) {
            m->StepFluidSimOnQueue(isInterior);
            // Forward live 256x256 GPU SWE water state + normals to RtWater (RtScene):
            if (m->rt) {
                const auto wDom = m->fluidSim.GetWaterDomainInfo();
                if (wDom.active && m->fluidSim.WaterStateResource() && m->fluidSim.WaterNormalResource()) {
                    m->rt->SetFluidWaterResources(
                        m->fluidSim.WaterStateResource(), m->fluidSim.WaterNormalResource(),
                        wDom.originXY[0], wDom.originXY[1], wDom.worldSpanU);
                }
            }
            // Composite live 64x64x64 GPU 3D Navier-Stokes fire/smoke/radiance directly onto colorOut with SceneDepth occlusion:
            const auto fDom = m->fluidSim.GetFireDomainInfo();
            if (m->fluidComposite.Ready() && fDom.active && fDom.emitterCount > 0u &&
                m->fluidSim.FireThermoResource() && m->fluidSim.FireRadianceResource()) {
                LightingFluidInputs fi{};
                LightingPassFluidInputs(m->pass.get(), &fi);
                FluidCompositeParams cp{};
                std::memcpy(cp.clipToWorld, &m->lighting.clipToWorld[0][0], 64u);
                cp.camPosW[0] = m->lighting.posAdjust[0];
                cp.camPosW[1] = m->lighting.posAdjust[1];
                cp.camPosW[2] = m->lighting.posAdjust[2];
                cp.simTimeSec = fi.simTimeSec;
                cp.domainMinW[0] = fDom.originXYZ[0];
                cp.domainMinW[1] = fDom.originXYZ[1];
                cp.domainMinW[2] = fDom.originXYZ[2];
                cp.domainSpanW = fDom.worldSpanU;
                cp.resScale[0] = m->lighting.resolutionScale[0];
                cp.resScale[1] = m->lighting.resolutionScale[1];
                cp.sunDir[0] = m->lighting.sunDirection[0];
                cp.sunDir[1] = m->lighting.sunDirection[1];
                cp.sunDir[2] = m->lighting.sunDirection[2];
                cp.sunIntensity = m->lighting.hasSun ? 1.0f : 0.25f;
                cp.sunCol[0] = m->lighting.sunColour[0];
                cp.sunCol[1] = m->lighting.sunColour[1];
                cp.sunCol[2] = m->lighting.sunColour[2];
                cp.ambientFill = isInterior ? 0.35f : 0.65f;
                cp.width = m->cfg.width;
                cp.height = m->cfg.height;
                cp.activeEmitters = fDom.emitterCount;
                cp.isInterior = isInterior;
                if (m->fluidComposite.Composite(
                        m->queue.Get(), m->colorOut.res12.Get(), m->depthIn.res12.Get(),
                        m->fluidSim.FireThermoResource(), m->fluidSim.FireRadianceResource(), cp)) {
                    if ((m->fluidCompFrames++ % 180ull) == 0ull) {
                        m->log("[FluidComposite] live 64^3 NS fire/smoke composited: emitters=%u, domainSpan=%.0fu",
                               fDom.emitterCount, fDom.worldSpanU);
                    }
                }
            }
        }
        if (runRadiance) {
            m->ExecuteRadianceScreenGiOnQueue();
        }
        if (runXpbd) {
            m->StepXpbdSimOnQueue(isInterior);
        }
        return SUCCEEDED(m->queue->Signal(m->fence12.Get(), v2));
    };

    // D3D11: copy in, hand over, take back, copy out.
    auto* c = m->ctx11.Get();
    c->End(s.ts[4].Get());
    c->CopyResource(m->colorIn.tex11.Get(), colorSrc);
    c->End(s.ts[5].Get());
    if (m->attached) {
        // One queue: the layer submits what D3D11 recorded so far with every shared texture in
        // COMMON, the pass follows on the same queue, and the copy-out lands in the layer's next list.
        std::vector<ID3D11Resource*> hand;
        bool rtOk = false;
        if (fc.rt) {
            if (m->bindless.Ready() && m->cfg.layer.native) {
                for (RtDraw& dr : m->pendingDraws) {
                    if (!dr.alphaSrv) continue;
                    ID3D11Resource* r11 = nullptr;
                    dr.alphaSrv->GetResource(&r11);
                    if (r11) {
                        if (ID3D12Resource* r12 = m->cfg.layer.native(r11)) {
                            D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
                            dr.alphaSrv->GetDesc(&sd);
                            dr.bindlessAlbedoIdx = m->bindless.RegisterTexture2D(r12, sd.Format);
                        }
                        r11->Release();
                    }
                }
            }
            m->rt->SetCamera(m->lighting.cameraStep, m->lighting.cameraSteps, m->lighting.posAdjust);
            m->rt->SetDraws(std::move(m->pendingDraws), hand, m->cfg.layer.native);
            const void* u = nullptr;
            size_t ub = 0;
            if (LightingPassUniform(m->pass.get(), &u, &ub))
                rtOk = m->rt->Record(u, ub, m->depthIn.res12.Get(), m->normalIn.res12.Get(),
                                     m->shared[static_cast<UINT>(Shared::Specular)].res12.Get(),
                                     fc.hasMotion ? m->motionIn.res12.Get() : nullptr, m->colorIn.res12.Get());
        }
        if (!m->HandToHost(hand) || (rtOk && !m->rt->Submit(m->queue.Get())) ||
            !m->pass->Submit(0, hasPostWork ? 0 : v2) || !runPostLightingPasses()) {
            m->log("[D3D12Bridge] attached hand-over failed; bridge off");
            c->End(s.disjoint.Get());
            s.open = false;
            m->ready = false;
            return false;
        }
        m->lastWaited = 0;
        m->lastD12 = v2;
        c->End(s.ts[6].Get());
        c->CopyResource(colorDst, m->colorOut.tex11.Get());
        c->End(s.ts[7].Get());
        c->End(s.disjoint.Get());
        s.open = false;
        s.executed = true;
        ++m->frames;
        m->EmitLiveTelemetry(fc.rt);
        StreamlineHost::Get().ReflexBeginFrame(static_cast<uint32_t>(m->frames));
        return true;
    }
    HRESULT hs = m->ctx4->Signal(m->fence11.Get(), v1);
    c->Flush();
    LARGE_INTEGER qf, q0, q1;
    QueryPerformanceFrequency(&qf);
    QueryPerformanceCounter(&q0);
    if (FAILED(hs)) {
        // Never queue a GPU wait the other side cannot satisfy.
        m->log("[D3D12Bridge] D3D11 Signal failed 0x%08X; bridge off", (unsigned)hs);
        c->End(s.disjoint.Get());
        s.open = false;
        m->ready = false;
        return false;
    }
    m->ProbeHead(frame, waitValue);
    if (!m->pass->Submit(waitValue, hasPostWork ? 0 : v2) || !runPostLightingPasses()) {
        // Nothing will signal v2 now, so D3D11 must not wait for it.
        m->log("[D3D12Bridge] pass '%s' failed to submit; bridge off", m->pass->Name());
        c->End(s.disjoint.Get());
        s.open = false;
        m->ready = false;
        return false;
    }
    m->ProbeTail(frame);
    QueryPerformanceCounter(&q1);
    // Already executed on the GPU when the D3D12 list was only now submitted: the queue idles on the CPU.
    m->caughtUp += c->GetData(s.ts[5].Get(), nullptr, 0, D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK;
    m->flushToSubmitUs += double(q1.QuadPart - q0.QuadPart) * 1e6 / double(qf.QuadPart);
    if (++m->handOvers == 300) {
        m->log("[D3D12Bridge] hand-over over 300 frames: the GPU had already reached it at submit in %u, CPU flush-to-submit mean %.1f us",
               m->caughtUp, m->flushToSubmitUs / 300.0);
        m->caughtUp = m->handOvers = 0;
        m->flushToSubmitUs = 0;
    }
    m->lastWaited = waitValue;
    m->lastD12 = v2;
    HRESULT hw = m->ctx4->Wait(m->fence11.Get(), v2);
    if (FAILED(hw)) m->log("[D3D12Bridge] D3D11 Wait failed 0x%08X", (unsigned)hw);
    c->End(s.ts[6].Get());
    c->CopyResource(colorDst, m->colorOut.tex11.Get());
    c->End(s.ts[7].Get());
    if (m->cfg.handoverFlush) c->Flush();
    c->End(s.disjoint.Get());
    s.open = false;
    s.executed = true;
    ++m->frames;
    m->EmitLiveTelemetry(fc.rt);
    StreamlineHost::Get().ReflexBeginFrame(static_cast<uint32_t>(m->frames));
    return true;
}

template <class T>
static bool GetQuery(ID3D11DeviceContext* c, ID3D11Query* q, T* out, bool block)
{
    const ULONGLONG deadline = GetTickCount64() + 2000;
    for (;;) {
        HRESULT hr = c->GetData(q, out, sizeof(T), block ? 0 : D3D11_ASYNC_GETDATA_DONOTFLUSH);
        if (hr == S_OK) return true;
        if (FAILED(hr) || !block || GetTickCount64() > deadline) return false;
        Sleep(0);
    }
}

bool D3D12Bridge::ReadTimings(uint64_t frame, bool block, BridgeTimings* out)
{
    if (!m->ready || !out) return false;
    D11Slot& s = m->SlotFor(frame);
    D12Slot& ds = m->d12[frame % kRing];
    if (s.frame != frame || !s.executed) return false;

    D3D11_QUERY_DATA_TIMESTAMP_DISJOINT dj{};
    if (!GetQuery(m->ctx11.Get(), s.disjoint.Get(), &dj, block) || dj.Disjoint) return false;
    uint64_t t[kD11Stamps]{};
    for (UINT i = 0; i < kD11Stamps; ++i) {
        if ((i < 2 && !s.hasDepth) || (i >= 2 && i < 4 && !s.hasNormal)) continue;
        if (!GetQuery(m->ctx11.Get(), s.ts[i].Get(), &t[i], block)) return false;
    }

    m->HarvestPassTimings();
    if (!(ds.havePass && ds.frameTag == frame) && block) {
        if (!m->pass->Retire(m->cfg.cpuWaitTimeout)) return false;
        m->HarvestPassTimings();
    }
    if (!(ds.havePass && ds.frameTag == frame)) return false;

    const double f11 = 1000.0 / static_cast<double>(dj.Frequency);
    BridgeTimings r{};
    r.frame = frame;
    r.depthCopyMs = s.hasDepth ? (t[1] - t[0]) * f11 : 0.0;
    r.normalCopyMs = s.hasNormal ? (t[3] - t[2]) * f11 : 0.0;
    r.copyInMs = (t[5] - t[4]) * f11;
    r.stallMs = (t[6] - t[5]) * f11;
    r.copyOutMs = (t[7] - t[6]) * f11;
    r.pass = ds.pass;
    r.passMs = ds.pass.totalMs;
    const double rtMs = m->rt ? (m->rt->Stats().buildMs + m->rt->Stats().traceMs + m->rt->Stats().cacheMs) : 0.0;
    const double gpuWorkMs = r.passMs + rtMs;
    r.syncMs = r.stallMs > gpuWorkMs ? r.stallMs - gpuWorkMs : 0.0;
    r.bridgeCostMs = r.depthCopyMs + r.normalCopyMs + r.copyInMs + r.copyOutMs + r.syncMs;
    m->ProbeRead(frame, dj.Frequency, t[5], t[6], &r);
    *out = r;
    return true;
}

uint64_t D3D12Bridge::FramesExecuted() const { return m->frames; }
void D3D12Bridge::SetWaitSkew(uint64_t skew) { m->skew = skew; }
bool D3D12Bridge::WaitForD3D12(DWORD timeoutMs) { return m->CpuWait(m->lastD12, timeoutMs); }
void D3D12Bridge::ForceSignal(uint64_t value) { if (m->fence12) m->fence12->Signal(value); }
uint64_t D3D12Bridge::LastD3D12Value() const { return m->lastD12; }
uint64_t D3D12Bridge::LastWaitedValue() const { return m->lastWaited; }
uint64_t D3D12Bridge::CompletedValue() const { return m->fence12 ? m->fence12->GetCompletedValue() : 0; }

} // namespace raw::bridge
